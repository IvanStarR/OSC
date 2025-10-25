// include/sst/footer.hpp
#pragma once
#include <cstdint>

namespace uringkv {

// v2 footer: добавили оффсет/размер разреженного индекса (sparse)
// и переименовали поля hash_* для явности.
struct SstFooter {
  uint64_t hash_index_offset; // начало HashIndexHeader
  uint32_t hash_table_size;   // кол-во слотов хеш-таблицы (степень двойки)
  uint32_t version;           // = 2
  uint64_t sparse_offset;     // начало sparse-индекса (ordered)
  uint32_t sparse_count;      // кол-во опорных точек в sparse
  char     magic[8];          // "URKVSST"
};

inline constexpr const char* kSstMagic   = "URKVSST";
inline constexpr uint32_t    kSstVersion = 2;

} // namespace uringkv
