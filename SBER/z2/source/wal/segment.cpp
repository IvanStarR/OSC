#include "wal/segment.hpp"
#include <algorithm>
#include <cstdio>

namespace uringkv {

std::string wal_segment_name(uint64_t index) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%06llu.wal", static_cast<unsigned long long>(index));
  return std::string(buf);
}

}
