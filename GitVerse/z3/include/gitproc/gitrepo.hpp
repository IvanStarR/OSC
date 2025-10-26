#pragma once
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace gitproc {

struct ServiceEntry {
  std::string name;
  std::string path;
};

class GitRepo {
public:
  GitRepo(std::string workdir);
  bool open_or_clone(const std::string &url_or_path, const std::string &branch);
  bool pull_reset();
  std::vector<ServiceEntry>
  scan_services(const std::string &rel_dir = "services") const;
  std::optional<std::string> read_file(const std::string &rel_path) const;

  bool checkout_file_at(const std::string &commit, const std::string &rel_file);

  const std::string &workdir() const { return workdir_; }

private:
  std::string workdir_;
  std::string branch_;
  bool is_bare_path_{false};
};

struct ServicesDiff {
  std::unordered_set<std::string> added;
  std::unordered_set<std::string> removed;
  std::unordered_set<std::string> changed;
};

ServicesDiff diff_sets(const std::vector<ServiceEntry> &old_list,
                       const std::vector<ServiceEntry> &new_list);

} // namespace gitproc
