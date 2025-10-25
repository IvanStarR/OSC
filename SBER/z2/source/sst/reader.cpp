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

  data_end_off_ = f.hash_index_offset;
  sparse_off_   = f.sparse_offset;
  sparse_cnt_   = f.sparse_count;

  // mmap hash-index block (header + table)
  (void)index_.open(fd_, f.hash_index_offset, f.hash_table_size);
  return true;
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

  // 2) fallback: linear scan up to data_end_off_ (records are 4KiB-blocked)
  uint64_t off = 0;
  while (off < data_end_off_) {
    SstRecordMeta m{}; std::string k; std::string v;
    if (!read_record_at(off, m, k, v)) break; // torn tail -> stop
    const uint64_t used   = sizeof(SstRecordMeta) + m.klen + m.vlen + sizeof(SstRecordTrailer);
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

// ---- sparse index helpers ----
bool SstReader::load_sparse_into(std::vector<std::pair<std::string,uint64_t>>& out) const {
  out.clear();
  if (fd_ < 0 || sparse_off_ == 0 || sparse_cnt_ == 0) return false;

  if (::lseek(fd_, (off_t)sparse_off_, SEEK_SET) < 0) return false;

  // SparseIndexHeader {magic,version,count} already known via footer; skip struct and read entries directly.
  // But for robustness, read and validate header again.
  struct SparseIndexHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t count;
  } sh{};

  if (::read(fd_, &sh, sizeof(sh)) != (ssize_t)sizeof(sh)) return false;
  if (sh.count != sparse_cnt_ || sh.magic != 0x53494458u || sh.version != 1u) return false;

  out.reserve(sh.count);
  for (uint32_t i=0; i<sh.count; ++i) {
    uint32_t klen = 0; uint64_t off = 0;
    if (::read(fd_, &klen, sizeof(klen)) != (ssize_t)sizeof(klen)) return false;
    if (::read(fd_, &off,  sizeof(off))  != (ssize_t)sizeof(off))  return false;
    std::string k; k.resize(klen);
    if (klen && ::read(fd_, k.data(), klen) != (ssize_t)klen) return false;
    out.emplace_back(std::move(k), off);
  }
  return true;
}

uint64_t SstReader::find_scan_start_offset(std::string_view start) const {
  if (start.empty()) return 0;

  std::vector<std::pair<std::string,uint64_t>> sparse;
  if (!load_sparse_into(sparse) || sparse.empty()) return 0;

  // binary search for greatest key <= start
  size_t lo = 0, hi = sparse.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    if (sparse[mid].first <= start) lo = mid + 1;
    else hi = mid;
  }
  if (lo == 0) return 0;
  return sparse[lo-1].second;
}

std::vector<std::pair<std::string, std::optional<std::string>>>
SstReader::scan(std::string_view start, std::string_view end) {
  std::vector<std::pair<std::string,std::optional<std::string>>> out;
  if (fd_ < 0) return out;

  uint64_t off = find_scan_start_offset(start);

  while (off < data_end_off_) {
    SstRecordMeta m{}; std::string k; std::string v;
    if (!read_record_at(off, m, k, v)) break; // stop on torn tail
    const uint64_t used   = sizeof(SstRecordMeta) + m.klen + m.vlen + sizeof(SstRecordTrailer);
    const uint64_t padded = (used + (SST_BLOCK_SIZE - 1)) & ~(SST_BLOCK_SIZE - 1);
    off += padded;

    if ((!start.empty() && k < start)) continue;
    if ((!end.empty()   && k > end))   break;

    if (m.flags == SST_FLAG_PUT) {
      out.emplace_back(std::move(k), std::optional<std::string>(std::move(v)));
    } else if (m.flags == SST_FLAG_DEL) {
      out.emplace_back(std::move(k), std::nullopt);
    }
  }
  // уже упорядочено по ключу в пределах SST
  return out;
}

} // namespace uringkv
