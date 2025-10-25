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

std::vector<std::pair<std::string, std::string>>
SstReader::scan(std::string_view start, std::string_view end) {
  // Linear pass with torn-tail protection & block stepping.
  std::vector<std::pair<std::string,std::string>> out;

  if (fd_ < 0) return out;

  off_t endpos = ::lseek(fd_, 0, SEEK_END);
  if (endpos < (off_t)sizeof(SstFooter)) return out;
  if (::lseek(fd_, endpos - (off_t)sizeof(SstFooter), SEEK_SET) < 0) return out;

  SstFooter f{};
  if (::read(fd_, &f, sizeof(f)) != (ssize_t)sizeof(f)) return out;

  uint64_t off = 0;
  while (off < f.index_offset) {
    SstRecordMeta m{}; std::string k; std::string v;
    if (!read_record_at(off, m, k, v)) break; // stop on torn tail
    const uint64_t used = sizeof(SstRecordMeta) + m.klen + m.vlen + sizeof(SstRecordTrailer);
    const uint64_t padded = (used + (SST_BLOCK_SIZE - 1)) & ~(SST_BLOCK_SIZE - 1);
    off += padded;

    if ((!start.empty() && k < start) || (!end.empty() && k > end)) {
      if (!end.empty() && k > end) break;
      continue;
    }
    if (m.flags == SST_FLAG_PUT) out.emplace_back(std::move(k), std::move(v));
  }
  std::sort(out.begin(), out.end(), [](auto& a, auto& b){ return a.first < b.first; });
  return out;
}

} // namespace uringkv
