#include "sst/writer.hpp"
#include "sst/footer.hpp"
#include "util.hpp"
#include <xxhash.h>
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <algorithm>
#include <cstring>

namespace uringkv {

SstWriter::SstWriter(const std::string& path) : path_(path) {
  fd_ = ::open(path_.c_str(), O_CREAT|O_TRUNC|O_WRONLY, 0644);
  if (fd_ < 0) spdlog::error("SST open failed: {}", path_);
}

SstWriter::~SstWriter() {
  if (fd_ >= 0) ::close(fd_);
}

bool SstWriter::write_sorted(
    const std::vector<std::pair<std::string, std::optional<std::string>>>& entries,
    uint32_t index_step)
{
  if (fd_ < 0) return false;

  for (size_t i=1;i<entries.size();++i)
    if (entries[i-1].first > entries[i].first)
      spdlog::warn("SST entries not sorted; writer will still proceed");

  uint64_t offset = 0;
  std::vector<std::pair<std::string,uint64_t>> sparse; sparse.reserve(entries.size()/index_step+4);

  for (size_t i=0;i<entries.size();++i) {
    const auto& kv = entries[i];
    const std::string& k = kv.first;
    const bool is_put = kv.second.has_value();
    const std::string_view v = is_put ? std::string_view(*kv.second) : std::string_view();

    // индекс каждые index_step записей
    if (i % index_step == 0) sparse.emplace_back(k, offset);

    XXH64_state_t* st = XXH64_createState();
    XXH64_reset(st, 0);
    XXH64_update(st, k.data(), k.size());
    XXH64_update(st, v.data(), v.size());
    const uint64_t h = static_cast<uint64_t>(XXH64_digest(st));
    XXH64_freeState(st);

    SstRecordMeta m{
      static_cast<uint32_t>(k.size()),
      static_cast<uint32_t>(v.size()),
      is_put ? SST_FLAG_PUT : SST_FLAG_DEL,
      h
    };

    struct iovec iov[3];
    iov[0].iov_base = const_cast<SstRecordMeta*>(&m); iov[0].iov_len = sizeof(m);
    iov[1].iov_base = const_cast<char*>(k.data());    iov[1].iov_len = k.size();
    iov[2].iov_base = const_cast<char*>(v.data());    iov[2].iov_len = v.size();

    ssize_t w = ::writev(fd_, iov, 3);
    if (w < 0) { spdlog::error("SST write failed"); return false; }
    offset += static_cast<uint64_t>(w);
  }

  // ---- записываем индексный блок ----
  const uint64_t index_offset = offset;

  uint32_t count = static_cast<uint32_t>(sparse.size());
  if (::write(fd_, &count, sizeof(count)) != (ssize_t)sizeof(count)) return false;

  for (auto& it : sparse) {
    uint32_t klen = static_cast<uint32_t>(it.first.size());
    uint64_t off  = it.second;
    if (::write(fd_, &klen, sizeof(klen)) != (ssize_t)sizeof(klen)) return false;
    if (::write(fd_, &off,  sizeof(off))  != (ssize_t)sizeof(off))  return false;
    if (klen && ::write(fd_, it.first.data(), klen) != (ssize_t)klen) return false;
  }

  // ---- футер ----
  SstFooter f{};
  f.index_offset = index_offset;
  f.index_count  = count;
  f.version      = kSstVersion;
  std::memset(f.magic, 0, sizeof(f.magic));
  std::memcpy(f.magic, kSstMagic, 7);

  if (::write(fd_, &f, sizeof(f)) != (ssize_t)sizeof(f)) return false;

  ::fsync(fd_);
  return true;
}

} // namespace uringkv
