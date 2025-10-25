#pragma once
#include <filesystem>
#include <string>

namespace gitproc {
namespace io {
  void ensure_dir(const std::filesystem::path& p);
  int  open_for_stdout(const std::filesystem::path& path); // O_APPEND
  int  open_for_stderr(const std::filesystem::path& path);

  // Ротация логов: если файл >= max_bytes — сдвигаем .N, переименовываем текущий в .1
  void rotate_logs(const std::filesystem::path& base_path,
                   std::uintmax_t max_bytes,
                   int backups);
}
} // namespace gitproc