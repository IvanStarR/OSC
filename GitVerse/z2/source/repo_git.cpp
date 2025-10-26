#include "gitconfig/kv.hpp"
#include "gitconfig/repo.hpp"
#include <string>
#include <sstream>
#include <cstdlib>

namespace gitconfig {

Repo::Repo(std::string path) : repo_path_(std::move(path)) {}

const std::string& Repo::path() const { return repo_path_; }

bool Repo::run_git(const std::string& args, std::string* out, int* code) const {
  std::string cmd = "git -C " + shell_quote(repo_path_) + " " + args + " 2>&1";
  auto r = run_sh(cmd);
  if (out) *out = r.out;
  if (code) *code = r.code;
  return r.code == 0;
}

bool Repo::ensure_branch(std::string* err) {
  std::string out; int code=0;
  if (!run_git("rev-parse --abbrev-ref HEAD", &out, &code)) {
    if (!run_git("checkout -B main", &out, &code)) {
      if (err) { *err = out; }
      return false;
    }
    return true;
  }
  if (out.find("HEAD") != std::string::npos) {
    if (!run_git("checkout -B main", &out, &code)) {
      if (err) { *err = out; }
      return false;
    }
  }
  return true;
}

bool Repo::ensure_initialized(std::string default_user_name, std::string default_user_email, std::string* err) {
  if (!dir_exists(repo_path_)) {
    if (!make_dirs(repo_path_, err)) return false;
  }
  std::string out; int code=0;
  if (!run_git("rev-parse --git-dir", &out, &code)) {
    if (!run_git("init", &out, &code)) {
      if (err) { *err = out; }
      return false;
    }
  }
  if (!run_git("config user.name", &out, &code) || out.size()==0) {
    if (!run_git("config user.name " + shell_quote(default_user_name), &out, &code)) {
      if (err) { *err = out; }
      return false;
    }
  }
  if (!run_git("config user.email", &out, &code) || out.size()==0) {
    if (!run_git("config user.email " + shell_quote(default_user_email), &out, &code)) {
      if (err) { *err = out; }
      return false;
    }
  }
  return ensure_branch(err);
}

bool Repo::add_path(const std::string& rel_path, std::string* err) {
  std::string out; int code=0;
  if (!run_git("add --all -- " + shell_quote(rel_path), &out, &code)) {
    if (err) { *err = out; }
    return false;
  }
  return true;
}

bool Repo::remove_path(const std::string& rel_path, std::string* err) {
  std::string out; int code=0;
  if (!run_git("rm -f -- " + shell_quote(rel_path), &out, &code)) {
    if (err) { *err = out; }
    return false;
  }
  return true;
}

bool Repo::has_uncommitted(std::string* err) const {
  std::string out; int code=0;
  if (!const_cast<Repo*>(this)->run_git("status --porcelain", &out, &code)) {
    if (err) { *err = out; }
    return false;
  }
  return !out.empty();
}

bool Repo::commit_all(const std::string& message, std::string* err) {
  std::string out; int code=0;
  if (!run_git("add -A", &out, &code)) {
    if (err) { *err = out; }
    return false;
  }
  if (!has_uncommitted(err)) return true;
  if (!run_git("commit -m " + shell_quote(message), &out, &code)) {
    if (err) { *err = out; }
    return false;
  }
  return true;
}

bool Repo::set_remote(const std::string& name, const std::string& url, std::string* err) {
  std::string out; int code=0;
  if (!run_git("remote get-url " + shell_quote(name), &out, &code)) {
    if (!run_git("remote add " + shell_quote(name) + " " + shell_quote(url), &out, &code)) {
      if (err) { *err = out; }
      return false;
    }
  } else {
    if (!run_git("remote set-url " + shell_quote(name) + " " + shell_quote(url), &out, &code)) {
      if (err) { *err = out; }
      return false;
    }
  }
  return true;
}

bool Repo::push(const std::string& remote, const std::string& branch, std::string* err) {
  std::string out; int code=0;
  if (!run_git("push -u " + shell_quote(remote) + " " + shell_quote(branch), &out, &code)) {
    if (err) { *err = out; }
    return false;
  }
  return true;
}

bool Repo::pull(const std::string& remote, const std::string& branch, std::string* err) {
  std::string out; int code=0;
  if (!run_git("pull --ff-only " + shell_quote(remote) + " " + shell_quote(branch), &out, &code)) {
    if (err) { *err = out; }
    return false;
  }
  return true;
}

std::optional<std::string> Repo::show_file_at(const std::string& rel_path, const std::string& commit, std::string* err) const {
  std::string out; int code=0;
  std::string arg = "show " + shell_quote(commit + ":" + rel_path);
  if (!run_git(arg, &out, &code)) {
    if (err) { *err = out; }
    return std::nullopt;
  }
  return out;
}

std::vector<std::pair<std::string,long long>> Repo::log_path(const std::string& rel_path, int limit, std::string* err) const {
  std::string out; int code=0;
  std::string fmt = "%H\t%ct";
  std::string lim = limit > 0 ? (" -n " + std::to_string(limit)) : "";
  std::string arg = "log --pretty=format:" + shell_quote(fmt) + lim + " -- " + shell_quote(rel_path);
  if (!run_git(arg, &out, &code)) {
    if (err) { *err = out; }
    return {};
  }
  std::vector<std::pair<std::string,long long>> v;
  std::istringstream ss(out);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.empty()) continue;
    auto tab = line.find('\t');
    if (tab == std::string::npos) continue;
    std::string sha = line.substr(0, tab);
    std::string ts = line.substr(tab+1);
    long long t = 0;
    try { t = std::stoll(ts); } catch (...) { t = 0; }
    v.push_back({sha, t});
  }
  return v;
}

bool Repo::fetch(const std::string& remote, const std::string& branch, std::string* err) {
  std::string out; int code=0;
  if (!run_git("fetch " + shell_quote(remote) + " " + shell_quote(branch), &out, &code)) {
    if (err) { *err = out; }
    return false;
  }
  return true;
}

int Repo::merge_no_commit(const std::string& rev, std::string* err) const {
  std::string out; int code=0;
  bool ok = const_cast<Repo*>(this)->run_git("merge --no-commit --no-ff " + shell_quote(rev), &out, &code);
  if (ok) return 0;
  std::string out2; int code2=0;
  const_cast<Repo*>(this)->run_git("diff --name-only --diff-filter=U", &out2, &code2);
  if (!out2.empty()) { if (err) { *err = out; } return 2; }
  if (err) { *err = out; }
  return -1;
}

std::vector<std::string> Repo::conflicted_files(std::string* err) const {
  std::string out; int code=0;
  if (!const_cast<Repo*>(this)->run_git("diff --name-only --diff-filter=U", &out, &code)) {
    if (err) { *err = out; }
    return {};
  }
  std::vector<std::string> v;
  std::istringstream ss(out);
  std::string line;
  while (std::getline(ss, line)) {
    if (!line.empty() && line.back()=='\n') line.pop_back();
    if (!line.empty()) v.push_back(line);
  }
  return v;
}

long long Repo::last_change_ts(const std::string& rev, const std::string& rel_path, std::string* err) const {
  std::string out; int code=0;
  std::string arg = "log -n 1 --pretty=%ct " + shell_quote(rev) + " -- " + shell_quote(rel_path);
  if (!const_cast<Repo*>(this)->run_git(arg, &out, &code)) {
    if (err) { *err = out; }
    return 0;
  }
  try { return std::stoll(out); } catch (...) { return 0; }
}

bool Repo::checkout_side(const std::string& rel_path, const std::string& side, std::string* err) {
  std::string out; int code=0;
  if (side=="ours") {
    if (!run_git("checkout --ours -- " + shell_quote(rel_path), &out, &code)) { if (err) { *err = out; } return false; }
  } else {
    if (!run_git("checkout --theirs -- " + shell_quote(rel_path), &out, &code)) { if (err) { *err = out; } return false; }
  }
  return true;
}

}
