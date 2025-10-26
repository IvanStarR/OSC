#pragma once
#include <string>
#include <vector>
#include <optional>
#include <utility>

namespace gitconfig {

struct RepoConfig {
  std::string repo_path;
  std::string data_root;
};

struct ListEntry {
  std::string key;
  bool is_dir;
};

class Repo {
public:
  explicit Repo(std::string path);
  bool ensure_initialized(std::string default_user_name, std::string default_user_email, std::string* err);
  bool ensure_branch(std::string* err);
  bool run_git(const std::string& args, std::string* out, int* code) const;
  const std::string& path() const;
  bool add_path(const std::string& rel_path, std::string* err);
  bool remove_path(const std::string& rel_path, std::string* err);
  bool has_uncommitted(std::string* err) const;
  bool commit_all(const std::string& message, std::string* err);
  bool set_remote(const std::string& name, const std::string& url, std::string* err);
  bool push(const std::string& remote, const std::string& branch, std::string* err);
  bool pull(const std::string& remote, const std::string& branch, std::string* err);
  std::optional<std::string> show_file_at(const std::string& rel_path, const std::string& commit, std::string* err) const;
  std::vector<std::pair<std::string,long long>> log_path(const std::string& rel_path, int limit, std::string* err) const;
  bool fetch(const std::string& remote, const std::string& branch, std::string* err);
  int merge_no_commit(const std::string& rev, std::string* err) const;
  std::vector<std::string> conflicted_files(std::string* err) const;
  long long last_change_ts(const std::string& rev, const std::string& rel_path, std::string* err) const;
  bool checkout_side(const std::string& rel_path, const std::string& side, std::string* err);
private:
  std::string repo_path_;
};

class KVStore {
public:
  explicit KVStore(RepoConfig cfg);
  bool init(std::string* err);
  bool set(const std::string& key, const std::string& value, std::string* err);
  std::optional<std::string> get(const std::string& key, std::string* err) const;
  std::optional<std::string> get_at(const std::string& key, const std::string& commit, std::string* err) const;
  bool erase(const std::string& key, std::string* err);
  std::vector<ListEntry> list(const std::string& prefix, bool recursive, std::string* err) const;
  std::string key_to_relpath(const std::string& key) const;
  static std::string now_iso8601();
  const Repo& repo() const;
  bool exists(const std::string& key) const;
  std::string data_root_rel() const;
private:
  RepoConfig cfg_;
  Repo repo_;
  std::string sanitize_key(const std::string& key) const;
  std::string join(const std::string& a, const std::string& b) const;
  bool write_file(const std::string& abs_path, const std::string& value, std::string* err) const;
  std::optional<std::string> read_file(const std::string& abs_path, std::string* err) const;
  bool remove_file(const std::string& abs_path, std::string* err) const;
  bool ensure_parent_dirs(const std::string& abs_path, std::string* err) const;
};

}
