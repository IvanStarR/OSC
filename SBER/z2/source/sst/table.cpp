#include "sst/table.hpp"
#include "util.hpp"
#include "sst/index.hpp"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

namespace uringkv {

SstTable::SstTable(std::string path) : path_(std::move(path)) {
  fd_ = ::open(path_.c_str(), O_RDONLY);
  if (fd_ >= 0) (void)load_footer_and_index();
}

SstTable::~SstTable() {
  index_.close();
  if (fd_ >= 0) ::close(fd_);
}

bool SstTable::load_footer_and_index() {
  if (fd_ < 0) return false;

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

bool SstTable::read_record_at(uint64_t off, SstRecordMeta& m, std::string& k, std::string& v) const {
  if (::lseek(fd_, (off_t)off, SEEK_SET) < 0) return false;
  if (::read(fd_, &m, sizeof(m)) != (ssize_t)sizeof(m)) return false;
  k.resize(m.klen); v.resize(m.vlen);
  if (m.klen && ::read(fd_, k.data(), m.klen) != (ssize_t)m.klen) return false;
  if (m.vlen && ::read(fd_, v.data(), m.vlen) != (ssize_t)m.vlen) return false;
  return true;
}

std::optional<std::pair<uint32_t, std::string>> SstTable::get(std::string_view key) const {
  if (!good()) return std::nullopt;

  const uint64_t h = sst_key_hash(key.data(), key.size());
  const uint64_t n = index_.table_size();
  const auto*    T = index_.table();
  const uint64_t mask = n - 1;

  // linear probing
  uint64_t pos = h & mask;
  for (uint64_t step=0; step<n; ++step) {
    const auto& e = T[pos];
    if (e.h == 0) break;            // пустой слот => не найдено
    if (e.h == h) {
      SstRecordMeta m{}; std::string k; std::string v;
      if (!read_record_at(e.off, m, k, v)) return std::nullopt;
      if (k == key) {
        if (m.checksum != dummy_checksum(k, v)) return std::nullopt;
        if (m.flags == SST_FLAG_DEL) return std::make_pair(SST_FLAG_DEL, std::string{});
        return std::make_pair(SST_FLAG_PUT, std::move(v));
      }
      // хеш совпал, но ключ другой — коллизия, пробуем дальше
    }
    pos = (pos + 1) & mask;
  }
  return std::nullopt;
}

} // namespace uringkv
