#include "sst/writer.hpp"
#include "util.hpp"
#include <xxhash.h>
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <algorithm>

namespace uringkv {

SstWriter::SstWriter(const std::string& path) : path_(path) {
  fd_ = ::open(path_.c_str(), O_CREAT|O_TRUNC|O_WRONLY, 0644);
  if (fd_ < 0) spdlog::error("SST open failed: {}", path_);
}

SstWriter::~SstWriter() {
  if (fd_ >= 0) ::close(fd_);
}

bool SstWriter::write_sorted(const std::vector<std::pair<std::string, std::optional<std::string>>>& entries) {
  if (fd_ < 0) return false;

  // Проверим, что отсортировано (assert-подобно; для MVP — мягко)
  for (size_t i=1;i<entries.size();++i)
    if (entries[i-1].first > entries[i].first)
      spdlog::warn("SST entries not sorted; writer will still proceed");

  for (auto& kv : entries) {
    const std::string& k = kv.first;
    const bool is_put = kv.second.has_value();
    const std::string_view v = is_put ? std::string_view(*kv.second) : std::string_view();

    XXH64_state_t* st = XXH64_createState();
    XXH64_reset(st, 0);
    XXH64_update(st, k.data(), k.size());
    XXH64_update(st, v.data(), v.size());
    uint64_t h = static_cast<uint64_t>(XXH64_digest(st));
    XXH64_freeState(st);

    SstRecordMeta m{
      static_cast<uint32_t>(k.size()),
      static_cast<uint32_t>(v.size()),
      is_put ? SST_FLAG_PUT : SST_FLAG_DEL,
      h
    };

    struct iovec iov[3];
    iov[0].iov_base = &m; iov[0].iov_len = sizeof(m);
    iov[1].iov_base = const_cast<char*>(k.data()); iov[1].iov_len = k.size();
    iov[2].iov_base = const_cast<char*>(v.data()); iov[2].iov_len = v.size();

    ssize_t w = ::writev(fd_, iov, 3);
    if (w < 0) { spdlog::error("SST write failed"); return false; }
  }

  ::fsync(fd_);
  return true;
}

} // namespace uringkv
