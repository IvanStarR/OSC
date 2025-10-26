#pragma once
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gitproc {

class GitRepo {
public:
  static GitRepo open(const std::string &path_or_url,
                      const std::filesystem::path &work_base = ".gitproc_work");

  static GitRepo open_local(const std::filesystem::path &root);

  bool is_remote() const { return is_remote_; }
  const std::filesystem::path &root() const { return root_; }

  std::filesystem::path resolve_unit(const std::string &target) const;

  std::optional<std::string> current_commit() const;

  bool pull(const std::string &branch);

  std::optional<std::filesystem::file_time_type>
  unit_revision(const std::filesystem::path &unit_path) const;

  bool has_unit_changed(const std::filesystem::path &unit_path,
                        bool *head_changed = nullptr);

private:
  explicit GitRepo(std::filesystem::path root, bool is_remote)
      : root_(std::move(root)), is_remote_(is_remote) {}

  static bool looks_like_url(const std::string &s);
  static bool looks_like_file_url(const std::string &s);
  static std::string file_url_to_path(const std::string &url);

  static std::string xxh3_64_hex(const std::string &s);
  static int exec_cmd(const std::vector<std::string> &args,
                      const std::filesystem::path &cwd,
                      std::string *out = nullptr);

  std::filesystem::path root_;
  bool is_remote_{false};

  std::unordered_map<std::string, std::string> last_hash_;

  int pull_failures_{0};
  std::chrono::steady_clock::time_point next_pull_allowed_{};
  std::optional<std::string> last_head_;
};

} // namespace gitproc
