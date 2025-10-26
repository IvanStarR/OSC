#include <gitproc/watcher.hpp>
#include <spdlog/spdlog.h>
#include <thread>

namespace fs = std::filesystem;

namespace gitproc {

bool Watcher::poll_once() {
  bool head_changed = repo_.pull(branch_);
  if (resolved_path_.empty()) {
    resolved_path_ = repo_.resolve_unit(unit_target_.string());
  }

  if (!primed_) {
    (void)repo_.has_unit_changed(resolved_path_);
    primed_ = true;
    return false;
  }

  bool content_changed = repo_.has_unit_changed(resolved_path_);
  if (head_changed || content_changed) {
    spdlog::info("[watch] change detected: reason={}",
                 head_changed ? "head_changed" : "content_changed");
    if (on_change_)
      on_change_(resolved_path_);
    return true;
  }
  return false;
}

void Watcher::run_loop(std::chrono::milliseconds period,
                       std::atomic_bool &stop_flag) {
  (void)poll_once();
  while (!stop_flag.load()) {
    std::this_thread::sleep_for(period);
    (void)poll_once();
  }
}

} // namespace gitproc
