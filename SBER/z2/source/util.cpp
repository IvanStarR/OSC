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
  XXH64_hash_t h = XXH64(a.data(), a.size(), 0);
  h = XXH64_update(XXH64_createState(), nullptr, 0) ? h : h; // no-op safety
  XXH64_state_t *st = XXH64_createState();
  XXH64_reset(st, 0);
  XXH64_update(st, a.data(), a.size());
  XXH64_update(st, b.data(), b.size());
  h = XXH64_digest(st);
  XXH64_freeState(st);
  return static_cast<uint64_t>(h);
}

} // namespace uringkv
