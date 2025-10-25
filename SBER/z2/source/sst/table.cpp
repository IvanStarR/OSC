#include "uringkv/sst/table.hpp"
#include "uringkv/util.hpp"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace uringkv {

SstTable::SstTable(std::string path) : path_(std::move(path)) {
  fd_ = ::open(path_.c_str(), O_RDONLY);
  if (fd_ >= 0) (void)load_footer_and_index();
}

SstTable::~SstTable() {
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

  if (::lseek(fd_, (off_t)f.index_offset, SEEK_SET) < 0) return false;
  uint32_t count = 0;
  if (::read(fd_, &count, sizeof(count)) != (ssize_t)sizeof(count)) return false;

  index_.clear(); index_.reserve(count);
  for (uint32_t i=0;i<count;++i) {
    uint32_t klen=0; uint64_t off=0;
    if (::read(fd_, &klen, sizeof(klen)) != (ssize_t)sizeof(klen)) return false;
    if (::read(fd_, &off,  sizeof(off))  != (ssize_t)sizeof(off))  return false;
    std::string key; key.resize(klen);
    if (klen && ::read(fd_, key.data(), klen) != (ssize_t)klen) return false;
    index_.push_back({std::move(key), off});
  }
  return !index_.empty();
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

  auto it = std::upper_bound(index_.begin(), index_.end(), key,
      [](std::string_view k, const SstIndexEntry& e){ return k < e.key; });
  if (it != index_.begin()) --it;

  SstRecordMeta m{}; std::string k; std::string v;
  uint64_t off = it->offset;

  while (true) {
    if (!read_record_at(off, m, k, v)) break;
    off += sizeof(m) + m.klen + m.vlen;

    if (k == key) {
      if (m.checksum != dummy_checksum(k, v)) return std::nullopt;
      if (m.flags == SST_FLAG_DEL) return std::make_pair(SST_FLAG_DEL, std::string{});
      return std::make_pair(SST_FLAG_PUT, std::move(v));
    }
    if (k > key) break;
  }
  return std::nullopt;
}

} // namespace uringkv
