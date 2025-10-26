#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gitproc/git.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <unistd.h>
#include <xxhash.h>

namespace fs = std::filesystem;

namespace gitproc {

static std::string join_shell(const std::vector<std::string> &args) {
  std::ostringstream oss;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i)
      oss << ' ';
    for (char c : args[i]) {
      if (c == ' ' || c == '"' || c == '\'')
        oss << '\\' << c;
      else
        oss << c;
    }
  }
  return oss.str();
}

int GitRepo::exec_cmd(const std::vector<std::string> &args, const fs::path &cwd,
                      std::string *out) {
  auto cmd = join_shell(args);
  std::string shell = "cd \"" + cwd.string() + "\" && " + cmd + " 2>&1";
  FILE *pipe = popen(shell.c_str(), "r");
  if (!pipe)
    return -1;
  std::string data;
  std::array<char, 4096> buf{};
  while (true) {
    size_t n = fread(buf.data(), 1, buf.size(), pipe);
    if (n == 0)
      break;
    data.append(buf.data(), n);
  }
  int rc = pclose(pipe);
  if (out)
    *out = data;
  return WEXITSTATUS(rc);
}

bool GitRepo::looks_like_url(const std::string &s) {
  return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0 ||
         s.rfind("git@", 0) == 0 || s.rfind("ssh://", 0) == 0;
}
bool GitRepo::looks_like_file_url(const std::string &s) {
  return s.rfind("file://", 0) == 0;
}
std::string GitRepo::file_url_to_path(const std::string &url) {
  // file:///abs/path  -> /abs/path
  if (!looks_like_file_url(url))
    return url;
  return url.substr(std::string("file://").size());
}

std::string GitRepo::xxh3_64_hex(const std::string &s) {
  auto h = XXH3_64bits(s.data(), s.size());
  std::ostringstream oss;
  oss << std::hex << h;
  return oss.str();
}

GitRepo GitRepo::open(const std::string &path_or_url,
                      const fs::path &work_base) {
  // file:// считаем «удалённым» источником (clone + pull)
  if (!looks_like_url(path_or_url) && !looks_like_file_url(path_or_url)) {
    return open_local(path_or_url);
  }
  fs::create_directories(work_base);
  std::string tag = xxh3_64_hex(path_or_url);
  fs::path dest = work_base / tag;

  if (!fs::exists(dest / ".git")) {
    fs::create_directories(dest.parent_path());
    std::string src = looks_like_file_url(path_or_url)
                          ? file_url_to_path(path_or_url)
                          : path_or_url;
    std::string out;
    int rc = exec_cmd({"git", "clone", "--depth", "1", src, dest.string()},
                      fs::current_path(), &out);
    if (rc != 0)
      throw std::runtime_error("git clone failed: " + out);
  }
  return GitRepo(fs::absolute(dest), true);
}

GitRepo GitRepo::open_local(const fs::path &root) {
  fs::path abs = fs::absolute(root);
  return GitRepo(abs, false);
}

std::filesystem::path GitRepo::resolve_unit(const std::string &target) const {
  fs::path t(target);
  if (t.is_absolute() || t.string().find('/') != std::string::npos ||
      t.extension() == ".unit" || t.extension() == ".service") {
    return t.is_absolute() ? t : (root_ / t);
  }
  fs::path cand1 = root_ / "services" / (target + ".service");
  fs::path cand2 = root_ / "services" / (target + ".unit");
  if (fs::exists(cand1))
    return cand1;
  if (fs::exists(cand2))
    return cand2;
  return cand2;
}

std::optional<std::string> GitRepo::current_commit() const {
  if (!fs::exists(root_ / ".git"))
    return std::nullopt;
  std::string out;
  int rc = exec_cmd({"git", "rev-parse", "HEAD"}, root_, &out);
  if (rc != 0)
    return std::nullopt;
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
    out.pop_back();
  return out;
}

bool GitRepo::pull(const std::string &branch) {
  if (!fs::exists(root_ / ".git"))
    return false;

  auto now = std::chrono::steady_clock::now();
  if (now < next_pull_allowed_) {
    return false;
  }

  auto before = current_commit();
  std::string out;

  int rc =
      exec_cmd({"git", "fetch", "--depth", "1", "origin", branch}, root_, &out);
  if (rc != 0) {
    pull_failures_ = std::min(pull_failures_ + 1, 10);
    int backoff =
        std::min(30, 1 << std::min(pull_failures_, 5)); // 1,2,4,8,16, then 30
    next_pull_allowed_ = now + std::chrono::seconds(backoff);
    spdlog::warn("[git] fetch failed (rc={}): backoff {}s", rc, backoff);
    return false;
  }

  rc = exec_cmd({"git", "reset", "--hard", "origin/" + branch}, root_, &out);
  if (rc != 0) {
    pull_failures_ = std::min(pull_failures_ + 1, 10);
    int backoff = std::min(30, 1 << std::min(pull_failures_, 5));
    next_pull_allowed_ = now + std::chrono::seconds(backoff);
    spdlog::warn("[git] reset failed (rc={}): backoff {}s", rc, backoff);
    return false;
  }

  pull_failures_ = 0;
  next_pull_allowed_ = now; // можно сразу
  auto after = current_commit();
  bool head_changed = (after != before);
  if (head_changed)
    last_head_ = after;
  return head_changed;
}

std::optional<fs::file_time_type>
GitRepo::unit_revision(const fs::path &unit_path) const {
  std::error_code ec;
  auto p = unit_path.is_absolute() ? unit_path : (root_ / unit_path);
  if (!fs::exists(p, ec))
    return std::nullopt;
  return fs::last_write_time(p, ec);
}

bool GitRepo::has_unit_changed(const fs::path &unit_path, bool *head_changed) {
  if (head_changed)
    *head_changed = false;

  auto p = unit_path.is_absolute() ? unit_path : (root_ / unit_path);
  std::error_code ec;
  if (!fs::exists(p, ec))
    return false;

  // читаем контент и хэшируем XXH3 (для unit-файлов ок)
  std::ifstream in(p, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  auto h = XXH3_64bits(data.data(), data.size());
  std::ostringstream oss;
  oss << std::hex << h;
  auto hex = oss.str();

  auto key = fs::absolute(p).string();
  auto it = last_hash_.find(key);
  if (it == last_hash_.end()) {
    last_hash_[key] = hex;
    return false; // первичное наблюдение — не изменение (см. Watcher::primed_)
  }
  if (it->second != hex) {
    it->second = hex;
    return true;
  }
  return false;
}

} // namespace gitproc
