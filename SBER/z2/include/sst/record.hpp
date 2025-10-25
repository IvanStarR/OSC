#pragma once
#include <cstdint>

namespace uringkv {

struct SstRecordMeta {
  uint32_t klen;
  uint32_t vlen;
  uint32_t flags;    // 1=PUT, 2=DEL
  uint64_t checksum; // XXH64(key||value)
};

static constexpr uint32_t SST_FLAG_PUT = 1u;
static constexpr uint32_t SST_FLAG_DEL = 2u;

} // namespace uringkv
