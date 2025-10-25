// include/sst/record.hpp
#pragma once
#include <cstdint>
#include <string_view>
#include <xxhash.h>

namespace uringkv {

struct SstRecordMeta {
  uint32_t klen;
  uint32_t vlen;
  uint32_t flags;    // 1=PUT, 2=DEL
  uint64_t checksum; // XXH64(key||value)
};

static constexpr uint32_t SST_FLAG_PUT = 1u;
static constexpr uint32_t SST_FLAG_DEL = 2u;

// ---- Torn-write protection for SST ----
// Each SST record is followed by a small trailer and padding to 4 KiB.
// That lets us safely detect a torn tail and stop scanning.
struct SstRecordTrailer {
  uint32_t rec_len;  // sizeof(SstRecordMeta)+klen+vlen
  uint32_t magic;    // 'SSTR' = 0x52545353
};

static constexpr uint32_t SST_TRAILER_MAGIC = 0x52545353u; // 'SSTR'
static constexpr uint64_t SST_BLOCK_SIZE    = 4096ull;

// Helper used across codebase (kept here for inlining)
inline uint64_t dummy_checksum(std::string_view a, std::string_view b) {
  XXH64_state_t* st = XXH64_createState();
  XXH64_reset(st, 0);
  if (!a.empty()) XXH64_update(st, a.data(), a.size());
  if (!b.empty()) XXH64_update(st, b.data(), b.size());
  const uint64_t h = static_cast<uint64_t>(XXH64_digest(st));
  XXH64_freeState(st);
  return h;
}

} // namespace uringkv
