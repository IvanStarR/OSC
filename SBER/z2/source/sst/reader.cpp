#include "sst/reader.hpp"
#include "sst/footer.hpp"
#include "sst/index.hpp"
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

SstReader::~SstReader() { index_.close(); if (fd_ >= 0) ::close(fd_); }

bool SstReader::load_footer_and_index() {
  if (fd_ < 0) return false;

  // читаем футер с конца
  off_t end = ::lseek(fd_, 0, SEEK_END);
  if (end < (off_t)sizeof(SstFooter)) return false;
  if (::lseek(fd_, end - (off_t)sizeof(SstFooter), SEEK_SET) < 0) return false;

  SstFooter f{};
  if (::read(fd_, &f, sizeof(f)) != (ssize_t)sizeof(f)) return false;
  if (std::memcmp(f.magic, kSstMagic, 7) != 0 || f.version != kSstVersion) return false;

  // mmap hash-index block
  if (!index_.open(fd_, f.index_offset, f.index_count)) return false;
  return index_.good();
}

bool SstReader::read_record_at(uint64_t off, SstRecordMeta& m, std::string& k, std::string& v) {
  if (::lseek(fd_, (off_t)off, SEEK_SET) < 0) return false;
  if (::read(fd_, &m, sizeof(m)) != (ssize_t)sizeof(m)) return false;
  k.resize(m.klen); v.resize(m.vlen);
  if (m.klen && ::read(fd_, k.data(), m.klen) != (ssize_t)m.klen) return false;
  if (m.vlen && ::read(fd_, v.data(), m.vlen) != (ssize_t)m.vlen) return false;
  return true;
}

std::optional<std::pair<uint32_t, std::string>> SstReader::get(std::string_view key) {
  if (fd_ < 0 || !index_.good()) return std::nullopt;

  const uint64_t h = sst_key_hash(key.data(), key.size());
  const uint64_t n = index_.table_size();
  const auto*    T = index_.table();
  const uint64_t mask = n - 1;

  uint64_t pos = h & mask;
  for (uint64_t step=0; step<n; ++step) {
    const auto& e = T[pos];
    if (e.h == 0) break;
    if (e.h == h) {
      SstRecordMeta m{}; std::string k; std::string v;
      if (!read_record_at(e.off, m, k, v)) return std::nullopt;
      if (k == key) {
        if (m.checksum != dummy_checksum(k, v)) return std::nullopt;
        if (m.flags == SST_FLAG_DEL) return std::make_pair(SST_FLAG_DEL, std::string{});
        return std::make_pair(SST_FLAG_PUT, std::move(v));
      }
    }
    pos = (pos + 1) & mask;
  }
  return std::nullopt;
}

std::vector<std::pair<std::string,std::string>> SstReader::scan(std::string_view start, std::string_view end) {
  // Для итерации 2 оставим простой линейный проход без использования индекса.
  // (Оптимизация скана будет в следующем пункте работ.)
  std::vector<std::pair<std::string,std::string>> out;

  if (fd_ < 0) return out;

  // Простой последовательный проход от начала данных до начала индекса (f.index_offset)
  off_t endpos = ::lseek(fd_, 0, SEEK_END);
  if (endpos < (off_t)sizeof(SstFooter)) return out;
  if (::lseek(fd_, endpos - (off_t)sizeof(SstFooter), SEEK_SET) < 0) return out;

  SstFooter f{};
  if (::read(fd_, &f, sizeof(f)) != (ssize_t)sizeof(f)) return out;

  uint64_t off = 0;
  while (off < f.index_offset) {
    SstRecordMeta m{}; std::string k; std::string v;
    if (!read_record_at(off, m, k, v)) break;
    off += sizeof(m) + m.klen + m.vlen;

    if ((!start.empty() && k < start) || (!end.empty() && k > end)) {
      if (!end.empty() && k > end) break;
      continue;
    }
    if (m.checksum != dummy_checksum(k, v)) break;
    if (m.flags == SST_FLAG_PUT) out.emplace_back(std::move(k), std::move(v));
  }
  std::sort(out.begin(), out.end(), [](auto& a, auto& b){ return a.first < b.first; });
  return out;
}

} // namespace uringkv
