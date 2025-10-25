// source/sst/reader.cpp
#include "sst/reader.hpp"
#include "sst/footer.hpp"
#include "sst/index.hpp"
#include "sst/record.hpp"
#include "util.hpp"

#include <xxhash.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstring>

namespace uringkv {

SstReader::SstReader(const std::string& path) : path_(path) {
  fd_ = ::open(path_.c_str(), O_RDONLY);
  if (fd_ >= 0) (void)load_footer_and_index();
}

SstReader::~SstReader() {
  index_.close();
  if (fd_ >= 0) ::close(fd_);
}

bool SstReader::load_footer_and_index() {
  if (fd_ < 0) return false;

  // read footer from end
  off_t end = ::lseek(fd_, 0, SEEK_END);
  if (end < (off_t)sizeof(SstFooter)) return false;
  if (::lseek(fd_, end - (off_t)sizeof(SstFooter), SEEK_SET) < 0) return false;

  SstFooter f{};
  if (::read(fd_, &f, sizeof(f)) != (ssize_t)sizeof(f)) return false;
  if (std::memcmp(f.magic, kSstMagic, 7) != 0 || f.version != kSstVersion) return false;

  // mmap hash-index block (header + table)
  if (!index_.open(fd_, f.index_offset, f.index_count)) {
    // leave index_ unopened; we'll fallback in getters
    return false;
  }
  return index_.good();
}

// read a record at exact offset; validate checksum & trailer
bool SstReader::read_record_at(uint64_t off, SstRecordMeta& m, std::string& k, std::string& v) {
  if (::lseek(fd_, (off_t)off, SEEK_SET) < 0) return false;

  if (::read(fd_, &m, sizeof(m)) != (ssize_t)sizeof(m)) return false;
  k.resize(m.klen); v.resize(m.vlen);
  if (m.klen && ::read(fd_, k.data(), m.klen) != (ssize_t)m.klen) return false;
  if (m.vlen && ::read(fd_, v.data(), m.vlen) != (ssize_t)m.vlen) return false;

  // trailer
  SstRecordTrailer tr{};
  if (::read(fd_, &tr, sizeof(tr)) != (ssize_t)sizeof(tr)) return false;
  const uint32_t expect_len = static_cast<uint32_t>(sizeof(SstRecordMeta) + m.klen + m.vlen);
  if (tr.magic != SST_TRAILER_MAGIC || tr.rec_len != expect_len) return false;

  // checksum verify (after we have full key/value)
  if (m.checksum != dummy_checksum(k, v)) return false;

  return true;
}

std::optional<std::pair<uint32_t, std::string>> SstReader::get(std::string_view key) {
  if (fd_ < 0) return std::nullopt;

  // 1) fast path via hash index if available
  if (index_.good()) {
    const uint64_t h0 = sst_key_hash(key.data(), key.size());
    const uint64_t h  = (h0 == 0) ? 1 : h0;
    const uint64_t n = index_.table_size();
    const auto*    T = index_.table();
    const uint64_t mask = n - 1;

    uint64_t pos = h & mask;
    for (uint64_t step = 0; step < n; ++step) {
      const auto& e = T[pos];
      if (e.h == 0) break; // empty slot => no such key
      if (e.h == h) {
        SstRecordMeta m{}; std::string k; std::string v;
        if (!read_record_at(e.off, m, k, v)) return std::nullopt;
        if (k == key) {
          if (m.flags == SST_FLAG_DEL) return std::make_pair(SST_FLAG_DEL, std::string{});
          return std::make_pair(SST_FLAG_PUT, std::move(v));
        }
        // collision — continue probing
      }
      pos = (pos + 1) & mask;
    }
    return std::nullopt;
  }

  // 2) fallback: linear scan up to index_offset (records are 4KiB-blocked)
  off_t endpos = ::lseek(fd_, 0, SEEK_END);
  if (endpos < (off_t)sizeof(SstFooter)) return std::nullopt;
  if (::lseek(fd_, endpos - (off_t)sizeof(SstFooter), SEEK_SET) < 0) return std::nullopt;

  SstFooter f{};
  if (::read(fd_, &f, sizeof(f)) != (ssize_t)sizeof(f)) return std::nullopt;

  uint64_t off = 0;
  while (off < f.index_offset) {
    SstRecordMeta m{}; std::string k; std::string v;
    if (!read_record_at(off, m, k, v)) break; // torn tail -> stop
    const uint64_t used = sizeof(SstRecordMeta) + m.klen + m.vlen + sizeof(SstRecordTrailer);
    const uint64_t padded = (used + (SST_BLOCK_SIZE - 1)) & ~(SST_BLOCK_SIZE - 1);
    off += padded;

    if (k == key) {
      if (m.flags == SST_FLAG_DEL) return std::make_pair(SST_FLAG_DEL, std::string{});
      return std::make_pair(SST_FLAG_PUT, std::move(v));
    }
    if (k > key) break; // early exit: keys are sorted
  }
  return std::nullopt;
}

// ---- Sparse index structures (must match writer) ----
struct SparseIndexHeader {
  uint32_t magic;   // 'SIDX'
  uint32_t version; // 1
  uint32_t count;   // number of entries
};

static constexpr uint32_t kSparseMagic   = 0x53494458u;
static constexpr uint32_t kSparseVersion = 1u;

static bool load_sparse_anchor(int fd, const SstFooter& f,
                               const HashIndexHeader* hash_hdr,
                               uint64_t& start_off,
                               std::string_view start_key)
{
  start_off = 0;

  if (!hash_hdr) return false;
  const uint64_t hash_bytes =
      sizeof(HashIndexHeader) + hash_hdr->table_size * sizeof(HashIndexEntry);
  const uint64_t sparse_pos = f.index_offset + hash_bytes;

  // Try read sparse header
  if (::lseek(fd, (off_t)sparse_pos, SEEK_SET) < 0) return false;

  SparseIndexHeader sh{};
  ssize_t r = ::read(fd, &sh, sizeof(sh));
  if (r != (ssize_t)sizeof(sh)) return false;
  if (sh.magic != kSparseMagic || sh.version != kSparseVersion || sh.count == 0) return false;

  // Linear read sparse entries and pick the greatest key <= start_key
  uint64_t best_off = 0;
  std::string best_key;

  for (uint32_t i = 0; i < sh.count; ++i) {
    uint32_t klen = 0;
    uint64_t off  = 0;
    if (::read(fd, &klen, sizeof(klen)) != (ssize_t)sizeof(klen)) return false;
    if (::read(fd, &off,  sizeof(off))  != (ssize_t)sizeof(off))  return false;

    std::string k; k.resize(klen);
    if (klen && ::read(fd, k.data(), klen) != (ssize_t)klen) return false;

    if (start_key.empty() || k <= start_key) {
      if (best_key.empty() || k > best_key) {
        best_key.swap(k);
        best_off = off;
      }
    }
  }

  start_off = best_off;
  return true;
}

std::vector<std::pair<std::string, std::string>>
SstReader::scan(std::string_view start, std::string_view end) {
  std::vector<std::pair<std::string,std::string>> out;
  if (fd_ < 0) return out;

  // read footer
  off_t endpos = ::lseek(fd_, 0, SEEK_END);
  if (endpos < (off_t)sizeof(SstFooter)) return out;
  if (::lseek(fd_, endpos - (off_t)sizeof(SstFooter), SEEK_SET) < 0) return out;

  SstFooter f{};
  if (::read(fd_, &f, sizeof(f)) != (ssize_t)sizeof(f)) return out;

  // try to leverage sparse index to jump close to 'start'
  uint64_t off = 0;
  const HashIndexHeader* hash_hdr = nullptr;
  if (index_.good()) hash_hdr = reinterpret_cast<const HashIndexHeader*>(
                                  reinterpret_cast<const char*>(index_.table()) - sizeof(HashIndexHeader));
  (void)load_sparse_anchor(fd_, f, hash_hdr, off, start);

  while (off < f.index_offset) {
    SstRecordMeta m{}; std::string k; std::string v;
    if (!read_record_at(off, m, k, v)) break; // stop on torn tail
    const uint64_t used = sizeof(SstRecordMeta) + m.klen + m.vlen + sizeof(SstRecordTrailer);
    const uint64_t padded = (used + (SST_BLOCK_SIZE - 1)) & ~(SST_BLOCK_SIZE - 1);
    off += padded;

    if ((!start.empty() && k < start)) continue;
    if ((!end.empty()   && k > end))   break;

    if (m.flags == SST_FLAG_PUT) out.emplace_back(std::move(k), std::move(v));
  }
  // records already arrive in order; sort is not necessary here
  return out;
}

} // namespace uringkv
