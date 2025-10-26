#pragma once
#include "git.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

namespace gitproc {

class Watcher {
public:
  using OnChange = std::function<void(const std::filesystem::path &unit_path)>;

  Watcher(GitRepo repo, std::filesystem::path unit_rel_or_path,
          std::string branch, OnChange cb)
      : repo_(std::move(repo)), unit_target_(std::move(unit_rel_or_path)),
        branch_(std::move(branch)), on_change_(std::move(cb)) {}

  bool poll_once();

  void run_loop(std::chrono::milliseconds period, std::atomic_bool &stop_flag);

  const std::filesystem::path &resolved_unit_path() const {
    return resolved_path_;
  }

private:
  GitRepo repo_;
  std::filesystem::path unit_target_;
  std::filesystem::path resolved_path_;
  std::string branch_;
  OnChange on_change_;
  bool primed_ = false;
};

} // namespace gitproc
