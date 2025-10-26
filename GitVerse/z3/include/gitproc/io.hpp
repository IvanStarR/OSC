#pragma once
#include <filesystem>
#include <string>

namespace gitproc {
namespace io {
  void ensure_dir(const std::filesystem::path& p);
  int  open_for_stdout(const std::filesystem::path& path);
  int  open_for_stderr(const std::filesystem::path& path);

  void rotate_logs(const std::filesystem::path& base_path,
                   std::uintmax_t max_bytes,
                   int backups);
}
} // namespace gitproc