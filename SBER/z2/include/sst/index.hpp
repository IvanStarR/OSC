#pragma once
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>

namespace uringkv {

// ---- On-disk hash index layout ----
// [HashIndexHeader]
// [HashIndexEntry table[table_size]]
// Footer (SstFooter) находится в самом конце файла.
// Footer.index_offset указывает на начало HashIndexHeader.
// Footer.index_count = table_size (кол-во слотов в таблице).
//
// Пустой слот: entry.h == 0

struct HashIndexHeader {
  uint32_t magic;      // 'HIDX' (0x48494458)
  uint32_t version;    // 1
  uint64_t table_size; // количество слотов (степень двойки)
  uint64_t num_items;  // фактически занятых слотов
};

struct HashIndexEntry {
  uint64_t h;    // XXH64(key, 0). 0 == empty
  uint64_t off;  // файловое смещение SstRecordMeta (начало записи)
};

inline constexpr uint32_t kHidxMagic   = 0x48494458u; // 'HIDX'
inline constexpr uint32_t kHidxVersion = 1u;

inline uint64_t sst_key_hash(const char* data, size_t len);

// ---- MMap wrapper over index block ----
class MmapHashIndex {
public:
  MmapHashIndex() = default;
  ~MmapHashIndex() { close(); }

  MmapHashIndex(const MmapHashIndex&) = delete;
  MmapHashIndex& operator=(const MmapHashIndex&) = delete;

  // Map [index_offset, index_offset + size(header)+table_size*sizeof(entry))] RO
  bool open(int fd, uint64_t index_offset, uint64_t table_size);

  void close();

  bool good() const { return hdr_ && table_ && table_size_ != 0; }

  uint64_t table_size() const { return table_size_; }
  const HashIndexEntry* table() const { return table_; }

private:
  void*     map_base_   = nullptr;
  size_t    map_len_    = 0;
  size_t    page_off_   = 0;

  const HashIndexHeader* hdr_   = nullptr;
  const HashIndexEntry*  table_ = nullptr;
  uint64_t table_size_ = 0;
};

} // namespace uringkv
