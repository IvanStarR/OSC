#include "sst/reader.hpp"
#include "sst/footer.hpp"
#include "util.hpp"
#include <xxhash.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstring>

namespace uringkv {

SstReader::SstReader(const std::string& path) : path_(path) {
  fd_ = ::open(path_.c_str(), O_RDONLY);
  if (fd_ >= 0) (void)load_footer_and_index();
}

SstReader::~SstReader() { if (fd_ >= 0) ::close(fd_); }

bool SstReader::load_footer_and_index() {
  if (fd_ < 0) return false;

  // читаем футер с конца
  off_t end = ::lseek(fd_, 0, SEEK_END);
  if (end < (off_t)sizeof(SstFooter)) return false;
  if (::lseek(fd_, end - (off_t)sizeof(SstFooter), SEEK_SET) < 0) return false;

  SstFooter f{};
  if (::read(fd_, &f, sizeof(f)) != (ssize_t)sizeof(f)) return false;
  if (std::memcmp(f.magic, kSstMagic, 7) != 0 || f.version != kSstVersion) return false;

  // читаем индексный блок
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

bool SstReader::read_record_at(uint64_t off, SstRecordMeta& m, std::string& k, std::string& v) {
  if (::lseek(fd_, (off_t)off, SEEK_SET) < 0) return false;
  if (::read(fd_, &m, sizeof(m)) != (ssize_t)sizeof(m)) return false;
  k.resize(m.klen); v.resize(m.vlen);
  if (m.klen && ::read(fd_, k.data(), m.klen) != (ssize_t)m.klen) return false;
  if (m.vlen && ::read(fd_, v.data(), m.vlen) != (ssize_t)m.vlen) return false;
  return true;
}

std::optional<std::pair<uint32_t, std::string>> SstReader::get(std::string_view key) {
  if (fd_ < 0) return std::nullopt;
  if (index_.empty()) return std::nullopt;

  // бинарный поиск по индексу
  auto it = std::upper_bound(index_.begin(), index_.end(), key,
      [](std::string_view k, const SstIndexEntry& e){ return k < e.key; });

  if (it != index_.begin()) --it; // ближайший <= key
  // теперь читаем от смещения `it->offset` последовательно, пока ключи не превысят искомый
  SstRecordMeta m{}; std::string k; std::string v;
  uint64_t off = it->offset;

  while (true) {
    off_t cur = ::lseek(fd_, 0, SEEK_CUR); (void)cur;
    if (!read_record_at(off, m, k, v)) break;
    // подготовим оффсет следующей записи
    off += sizeof(m) + m.klen + m.vlen;

    if (k == key) {
      // проверим checksum
      if (m.checksum != dummy_checksum(k, v)) return std::nullopt;
      if (m.flags == SST_FLAG_DEL) return std::make_pair(SST_FLAG_DEL, std::string{});
      return std::make_pair(SST_FLAG_PUT, std::move(v));
    }
    if (k > key) break; // прошли нужный ключ
  }

  return std::nullopt;
}

std::vector<std::pair<std::string,std::string>> SstReader::scan(std::string_view start, std::string_view end) {
  std::vector<std::pair<std::string,std::string>> out;
  if (fd_ < 0 || index_.empty()) return out;

  // найдём первую точку индекса для start
  auto it = index_.begin();
  if (!start.empty()) {
    it = std::lower_bound(index_.begin(), index_.end(), start,
        [](const SstIndexEntry& e, std::string_view s){ return e.key < s; });
    if (it != index_.begin()) --it;
  }

  // линейно читаем записи, пока ключ <= end
  uint64_t off = it->offset;
  while (true) {
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
