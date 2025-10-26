#pragma once
#include "unit.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace gitproc {

struct ProcStatus {
  bool running{false};
  int pid{-1};
  int last_exit_code{0};
};

class ProcessRunner {
public:
  static int start(const Unit &u, const std::filesystem::path &run_dir,
                   const std::filesystem::path &logs_dir);
  static bool stop(const Unit &u, const std::filesystem::path &run_dir);
  static ProcStatus status(const Unit &u, const std::filesystem::path &run_dir);

  static bool reload(const Unit &u, const std::filesystem::path &run_dir,
                     const std::filesystem::path &logs_dir);
};

} // namespace gitproc
