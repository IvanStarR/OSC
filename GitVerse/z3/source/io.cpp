#include <fcntl.h>
#include <filesystem>
#include <gitproc/io.hpp>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace gitproc {
namespace io {

void ensure_dir(const fs::path &p) {
  if (!fs::exists(p))
    fs::create_directories(p);
}

void rotate_logs(const fs::path &base_path, std::uintmax_t max_bytes,
                 int backups) {
  std::error_code ec;
  if (!fs::exists(base_path, ec))
    return;
  auto sz = fs::file_size(base_path, ec);
  if (ec || sz < max_bytes)
    return;

  for (int i = backups - 1; i >= 1; --i) {
    fs::path src = base_path;
    src += "." + std::to_string(i);
    fs::path dst = base_path;
    dst += "." + std::to_string(i + 1);
    if (fs::exists(src)) {
      std::error_code e2;
      fs::rename(src, dst, e2);
    }
  }
  fs::path first = base_path;
  first += ".1";
  {
    std::error_code e3;
    fs::rename(base_path, first, e3);
  }
  int fd = ::open(base_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd >= 0)
    ::close(fd);
}

static int open_append(const fs::path &path) {
  int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (fd < 0)
    throw std::runtime_error("open: " + path.string());
  return fd;
}

int open_for_stdout(const fs::path &path) { return open_append(path); }
int open_for_stderr(const fs::path &path) { return open_append(path); }

} // namespace io
} // namespace gitproc
