#include "sst/manifest.hpp"
#include "util.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace uringkv {

bool read_current(const std::string& sst_dir, uint64_t& last_index) {
  auto cur = join_path(sst_dir, "CURRENT");
  int fd = ::open(cur.c_str(), O_RDONLY);
  if (fd < 0) return false;
  char buf[64]{};
  ssize_t r = ::read(fd, buf, sizeof(buf)-1);
  ::close(fd);
  if (r <= 0) return false;
  last_index = std::strtoull(buf, nullptr, 10);
  return true;
}

static inline void fsync_dir_path(const std::string& dir) {
  int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
  if (dfd >= 0) { (void)::fsync(dfd); ::close(dfd); }
}

bool write_current_atomic(const std::string& sst_dir, uint64_t last_index) {
  auto tmp = join_path(sst_dir, "CURRENT.tmp");
  auto cur = join_path(sst_dir, "CURRENT");

  int fd = ::open(tmp.c_str(), O_CREAT|O_TRUNC|O_WRONLY, 0644);
  if (fd < 0) return false;

  char buf[64];
  int n = std::snprintf(buf, sizeof(buf), "%llu\n", (unsigned long long)last_index);
  bool ok = (::write(fd, buf, n) == n);

  // гарантируем запись содержимого файла
  ::fsync(fd);
  ::close(fd);

  if (!ok) { ::unlink(tmp.c_str()); return false; }

  // атомарная переклейка имени
  if (::rename(tmp.c_str(), cur.c_str()) != 0) {
    ::unlink(tmp.c_str());
    return false;
  }

  // ВАЖНО: гарантируем запись записи каталога (link+rename) на диск
  fsync_dir_path(sst_dir);
  return true;
}

std::string sst_name(uint64_t index) {
  char b[32];
  std::snprintf(b, sizeof(b), "%06llu.sst", (unsigned long long)index);
  return std::string(b);
}

std::vector<std::string> list_sst_sorted(const std::string& sst_dir) {
  std::vector<std::string> out;
  DIR* d = ::opendir(sst_dir.c_str());
  if (!d) return out;
  while (auto* e = ::readdir(d)) {
    std::string n = e->d_name;
    if (n.size() == 10 && n.substr(6) == ".sst") {
      bool digits = std::all_of(n.begin(), n.begin()+6, ::isdigit);
      if (digits) out.push_back(n);
    }
  }
  ::closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

} // namespace uringkv
