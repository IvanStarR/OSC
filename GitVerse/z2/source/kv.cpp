#include "gitconfig/kv.hpp"
#include "gitconfig/repo.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace gitconfig {

KVStore::KVStore(RepoConfig cfg) : cfg_(std::move(cfg)), repo_(cfg_.repo_path) {}

const Repo& KVStore::repo() const { return repo_; }

std::string KVStore::data_root_rel() const { return cfg_.data_root; }

std::string KVStore::join(const std::string& a, const std::string& b) const {
  if (a.empty()) return b;
  if (a.back()=='/') return a + b;
  return a + "/" + b;
}

std::string KVStore::sanitize_key(const std::string& key) const {
  std::string k = key;
  if (k.empty()) return "/";
  if (k[0] != '/') k = "/" + k;
  std::string out;
  int dots = 0;
  for (size_t i=0;i<k.size();++i) {
    char c = k[i];
    if (c=='\\') c = '/';
    if (c=='\0') continue;
    if (c=='/') { dots=0; out.push_back('/'); continue; }
    if (c=='.') {
      dots++;
      if (dots>=2) continue;
      out.push_back('.');
    } else {
      dots=0;
      out.push_back(c);
    }
  }
  return out;
}

std::string KVStore::key_to_relpath(const std::string& key) const {
  std::string k = sanitize_key(key);
  if (k.size()>1 && k[0]=='/') k.erase(0,1);
  return join(cfg_.data_root, k);
}

bool KVStore::ensure_parent_dirs(const std::string& abs_path, std::string* err) const {
  std::filesystem::path p(abs_path);
  p = p.parent_path();
  return make_dirs(p.string(), err);
}

bool KVStore::write_file(const std::string& abs_path, const std::string& value, std::string* err) const {
  std::ofstream f(abs_path, std::ios::binary|std::ios::trunc);
  if (!f.is_open()) { if (err) *err = "open failed: " + abs_path; return false; }
  f.write(value.data(), static_cast<std::streamsize>(value.size()));
  if (!f.good()) { if (err) *err = "write failed: " + abs_path; return false; }
  return true;
}

std::optional<std::string> KVStore::read_file(const std::string& abs_path, std::string* err) const {
  (void)err;
  std::ifstream f(abs_path, std::ios::binary);
  if (!f.is_open()) { return std::nullopt; }
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool KVStore::remove_file(const std::string& abs_path, std::string* err) const {
  std::error_code ec;
  bool ok = std::filesystem::remove(abs_path, ec);
  if (ec) { if (err) *err = "remove failed: " + abs_path; return false; }
  return ok;
}

std::string KVStore::now_iso8601() {
  using namespace std::chrono;
  auto now = system_clock::now();
  auto t = system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return std::string(buf);
}

bool KVStore::init(std::string* err) {
  if (!repo_.ensure_initialized("gitconfig", "gitconfig@localhost", err)) return false;
  std::string dr = std::filesystem::path(repo_.path()) / cfg_.data_root;
  if (!make_dirs(dr, err)) return false;
  return true;
}

bool KVStore::set(const std::string& key, const std::string& value, std::string* err) {
  if (!init(err)) return false;
  std::string rel = key_to_relpath(key);
  std::string abs = std::filesystem::path(repo_.path()) / rel;
  if (!ensure_parent_dirs(abs, err)) return false;
  if (!write_file(abs, value, err)) return false;
  if (!repo_.add_path(rel, err)) return false;
  std::string msg = "set key=" + sanitize_key(key) + " ts=" + now_iso8601();
  return repo_.commit_all(msg, err);
}

std::optional<std::string> KVStore::get(const std::string& key, std::string* err) const {
  (void)err;
  std::string rel = key_to_relpath(key);
  std::string abs = std::filesystem::path(repo_.path()) / rel;
  return read_file(abs, nullptr);
}

std::optional<std::string> KVStore::get_at(const std::string& key, const std::string& commit, std::string* err) const {
  std::string rel = key_to_relpath(key);
  return repo_.show_file_at(rel, commit, err);
}

bool KVStore::erase(const std::string& key, std::string* err) {
  if (!init(err)) return false;
  std::string rel = key_to_relpath(key);
  std::string abs = std::filesystem::path(repo_.path()) / rel;
  remove_file(abs, nullptr);
  repo_.remove_path(rel, nullptr);
  std::string msg = "delete key=" + sanitize_key(key) + " ts=" + now_iso8601();
  return repo_.commit_all(msg, err);
}

std::vector<ListEntry> KVStore::list(const std::string& prefix, bool recursive, std::string* err) const {
  (void)err;
  std::vector<ListEntry> out;
  std::string rel = key_to_relpath(prefix);
  std::string abs_root = std::filesystem::path(repo_.path()) / rel;
  if (!dir_exists(abs_root)) return out;
  std::filesystem::path base(repo_.path());
  std::filesystem::path data_root_abs = base / cfg_.data_root;
  if (recursive) {
    for (auto const& e : std::filesystem::recursive_directory_iterator(abs_root)) {
      if (std::filesystem::is_directory(e.path())) continue;
      auto relp = std::filesystem::relative(e.path(), data_root_abs).string();
      std::string key = "/" + relp;
      out.push_back({key, false});
    }
  } else {
    for (auto const& e : std::filesystem::directory_iterator(abs_root)) {
      auto relp = std::filesystem::relative(e.path(), data_root_abs).string();
      std::string key = "/" + relp;
      bool isdir = std::filesystem::is_directory(e.path());
      out.push_back({key, isdir});
    }
  }
  return out;
}

bool KVStore::exists(const std::string& key) const {
  std::string rel = key_to_relpath(key);
  std::string abs = std::filesystem::path(repo_.path()) / rel;
  return file_exists(abs);
}

}
