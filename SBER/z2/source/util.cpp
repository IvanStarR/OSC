#include "util.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <xxhash.h>

namespace uringkv {

bool ensure_dir(const std::string &p) {
  struct stat st {};
  if (stat(p.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return ::mkdir(p.c_str(), 0700) == 0;
}

std::string join_path(std::string a, std::string b) {
  if (!a.empty() && a.back() != '/')
    a.push_back('/');
  a += b;
  return a;
}

uint64_t dummy_checksum(std::string_view a, std::string_view b) {
  // Реальная XXH64: hash(key || value) с seed=0.
  XXH64_state_t* st = XXH64_createState();
  if (!st) return 0;
  XXH64_reset(st, 0);
  if (!a.empty()) XXH64_update(st, a.data(), a.size());
  if (!b.empty()) XXH64_update(st, b.data(), b.size());
  const uint64_t h = static_cast<uint64_t>(XXH64_digest(st));
  XXH64_freeState(st);
  return h;
}

} // namespace uringkv
