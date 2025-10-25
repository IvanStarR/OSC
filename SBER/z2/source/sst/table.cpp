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

  // Пытаемся замапить хеш-индекс; если не вышло — good() вернёт false, но у нас есть fallback.
  (void)index_.open(fd_, f.index_offset, f.index_count);
  return true;
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
  if (fd_ < 0) return std::nullopt;

  // 1) Быстрый путь: через mmap-хеш-индекс
  if (index_.good()) {
    const uint64_t h = sst_key_hash(key.data(), key.size());
    const uint64_t n = index_.table_size();
    const auto*    T = index_.table();
    const uint64_t mask = n - 1;

    uint64_t pos = h & mask;
    for (uint64_t step=0; step<n; ++step) {
      const auto& e = T[pos];
      if (e.h == 0) break; // пустой слот — точно нет ключа
      if (e.h == h) {
        SstRecordMeta m{}; std::string k; std::string v;
        if (!read_record_at(e.off, m, k, v)) return std::nullopt;
        if (k == key) {
          if (m.checksum != dummy_checksum(k, v)) return std::nullopt;
          if (m.flags == SST_FLAG_DEL) return std::make_pair(SST_FLAG_DEL, std::string{});
          return std::make_pair(SST_FLAG_PUT, std::move(v));
        }
        // коллизия — пробуем дальше
      }
      pos = (pos + 1) & mask;
    }
    return std::nullopt;
  }

  // 2) Fallback: линейный проход по данным до начала индекса (медленнее, но корректно)
  //   читаем футер ещё раз, чтобы узнать offset индекса
  off_t end = ::lseek(fd_, 0, SEEK_END);
  if (end < (off_t)sizeof(SstFooter)) return std::nullopt;
  if (::lseek(fd_, end - (off_t)sizeof(SstFooter), SEEK_SET) < 0) return std::nullopt;

  SstFooter f{};
  if (::read(fd_, &f, sizeof(f)) != (ssize_t)sizeof(f)) return std::nullopt;

  uint64_t off = 0;
  while (off < f.index_offset) {
    SstRecordMeta m{}; std::string k; std::string v;
    if (!read_record_at(off, m, k, v)) break;
    off += sizeof(m) + m.klen + m.vlen;

    if (k == key) {
      if (m.checksum != dummy_checksum(k, v)) return std::nullopt;
      if (m.flags == SST_FLAG_DEL) return std::make_pair(SST_FLAG_DEL, std::string{});
      return std::make_pair(SST_FLAG_PUT, std::move(v));
    }

    if (k > key) break; // записи отсортированы по ключу
  }

  return std::nullopt;
}

} // namespace uringkv
