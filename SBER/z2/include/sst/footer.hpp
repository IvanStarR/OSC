#pragma once
#include <cstdint>

namespace uringkv {

struct SstFooter {
  uint64_t index_offset; // смещение начала индексного блока
  uint32_t index_count;  // сколько точек в индексе
  uint32_t version;      // 1
  char     magic[8];     // "URKVSST"
};

inline constexpr const char* kSstMagic = "URKVSST";
inline constexpr uint32_t    kSstVersion = 1;

} // namespace uringkv
