#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace uringkv {

struct SstIndexEntry {
  std::string key;
  uint64_t offset; // смещение начала SstRecordMeta
};

using SstIndex = std::vector<SstIndexEntry>;

} // namespace uringkv
