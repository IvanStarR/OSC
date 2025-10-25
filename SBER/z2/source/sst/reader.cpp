#include "sst/reader.hpp"
#include "util.hpp"
#include <xxhash.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <vector>
#include <unordered_map>

namespace uringkv {

SstReader::SstReader(const std::string& path) : path_(path) {
  fd_ = ::open(path_.c_str(), O_RDONLY);
}

SstReader::~SstReader() { if (fd_ >= 0) ::close(fd_); }

std::optional<std::pair<uint32_t, std::string>> SstReader::get(std::string_view key) {
  if (fd_ < 0) return std::nullopt;
  // Пройдём линейно; сохраним последнее состояние ключа
  std::optional<std::pair<uint32_t, std::string>> state;
  off_t off = 0;
  const size_t bufk = 4096;
  (void)bufk;

  while (true) {
    SstRecordMeta m{};
    ssize_t r = ::read(fd_, &m, sizeof(m));
    if (r == 0) break;
    if (r != (ssize_t)sizeof(m)) break;
    std::string k; k.resize(m.klen);
    if (m.klen && ::read(fd_, k.data(), m.klen) != (ssize_t)m.klen) break;
    std::string v; v.resize(m.vlen);
    if (m.vlen && ::read(fd_, v.data(), m.vlen) != (ssize_t)m.vlen) break;
    if (m.checksum != dummy_checksum(k, v)) break;

    if (k == key) {
      if (m.flags == SST_FLAG_PUT) state = std::make_pair(SST_FLAG_PUT, std::move(v));
      else state = std::make_pair(SST_FLAG_DEL, std::string{});
    }
  }
  return state;
}

std::vector<std::pair<std::string,std::string>> SstReader::scan(std::string_view start, std::string_view end) {
  std::vector<std::pair<std::string,std::string>> out;
  if (fd_ < 0) return out;

  // финальное состояние ключей в файле
  std::unordered_map<std::string, std::pair<uint32_t,std::string>> last;
  while (true) {
    SstRecordMeta m{};
    ssize_t r = ::read(fd_, &m, sizeof(m));
    if (r == 0) break;
    if (r != (ssize_t)sizeof(m)) break;
    std::string k; k.resize(m.klen);
    if (m.klen && ::read(fd_, k.data(), m.klen) != (ssize_t)m.klen) break;
    std::string v; v.resize(m.vlen);
    if (m.vlen && ::read(fd_, v.data(), m.vlen) != (ssize_t)m.vlen) break;
    if (m.checksum != dummy_checksum(k, v)) break;

    last[std::move(k)] = {m.flags, std::move(v)};
  }

  // отфильтруем диапазон и tombstones
  for (auto& kv : last) {
    const std::string& k = kv.first;
    if ((!start.empty() && k < start) || (!end.empty() && k > end)) continue;
    if (kv.second.first == SST_FLAG_PUT) out.emplace_back(k, kv.second.second);
  }
  // порядок не критичен для MVP (можно отсортировать)
  std::sort(out.begin(), out.end(), [](auto& a, auto& b){ return a.first < b.first; });
  return out;
}

} // namespace uringkv
