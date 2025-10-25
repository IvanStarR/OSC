#include "util.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace uringkv {

bool ensure_dir(const std::string& p) {
  struct stat st{};
  if (stat(p.c_str(), &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return ::mkdir(p.c_str(), 0700) == 0;
}

std::string join_path(std::string a, std::string b) {
  if (!a.empty() && a.back() != '/') a.push_back('/');
  a += b; return a;
}

uint64_t dummy_checksum(std::string_view a, std::string_view b) {
  uint64_t h = 1469598103934665603ull;
  auto mix=[&](char c){ h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; };
  for(char c: a) mix(c);
  for(char c: b) mix(c);
  return h;
}

} // namespace uringkv
