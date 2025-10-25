#include "sst/index.hpp"
#include <xxhash.h>
#include <sys/mman.h>
#include <cerrno>
#include <cstring>
#include <algorithm>

namespace uringkv {

inline size_t page_size_cached() {
  static size_t ps = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
  return ps ? ps : 4096;
}

uint64_t sst_key_hash(const char* data, size_t len) {
  return static_cast<uint64_t>(XXH64(data, len, 0));
}

bool MmapHashIndex::open(int fd, uint64_t index_offset, uint64_t table_sz) {
  close();

  const size_t ps = page_size_cached();
  const size_t off_page = static_cast<size_t>(index_offset % ps);
  const off_t  map_off  = static_cast<off_t>(index_offset - off_page);

  const size_t need = sizeof(HashIndexHeader) + table_sz * sizeof(HashIndexEntry);
  const size_t map_len = off_page + need;

  void* p = ::mmap(nullptr, map_len, PROT_READ, MAP_SHARED, fd, map_off);
  if (p == MAP_FAILED) return false;

  map_base_ = p;
  map_len_  = map_len;
  page_off_ = off_page;

  auto* hdr = reinterpret_cast<const HashIndexHeader*>(static_cast<const char*>(p) + off_page);
  if (!hdr || hdr->magic != kHidxMagic || hdr->version != kHidxVersion || hdr->table_size != table_sz) {
    close();
    return false;
  }

  hdr_ = hdr;
  table_ = reinterpret_cast<const HashIndexEntry*>(hdr_ + 1);
  table_size_ = hdr_->table_size;
  return true;
}

void MmapHashIndex::close() {
  if (map_base_) {
    ::munmap(map_base_, map_len_);
  }
  map_base_ = nullptr;
  map_len_  = 0;
  page_off_ = 0;
  hdr_      = nullptr;
  table_    = nullptr;
  table_size_ = 0;
}

} // namespace uringkv
