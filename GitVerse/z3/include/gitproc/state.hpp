#pragma once
#include <filesystem>
#include <string>
#include <optional>

namespace gitproc {
struct StateStore {
  static std::filesystem::path pid_file(const std::string& name,
                                        const std::filesystem::path& run_dir);
  static std::filesystem::path status_file(const std::string& name,
                                           const std::filesystem::path& run_dir);
  static bool write_pid(const std::filesystem::path& f, int pid);
  static std::optional<int> read_pid(const std::filesystem::path& f);
  static void write_status_json(const std::filesystem::path& f, int pid, int exit_code);
};
} // namespace gitproc
