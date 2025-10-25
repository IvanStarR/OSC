#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <sys/uio.h> 

namespace uringkv {

struct WalRecordMeta {
  uint32_t klen;
  uint32_t vlen;
  uint32_t flags; // 1=PUT, 2=DEL
  uint64_t seqno;
  uint64_t checksum; // XXH64(key||value)
};

class WalWriter {
public:
  explicit WalWriter(const std::string &wal_path, bool use_uring = false,
                     unsigned uring_qd = 256);
  ~WalWriter();

  WalWriter(const WalWriter &) = delete;
  WalWriter &operator=(const WalWriter &) = delete;
  WalWriter(WalWriter &&other) noexcept;
  WalWriter &operator=(WalWriter &&other) noexcept;

  bool append_put(uint64_t seqno, std::string_view k, std::string_view v);
  bool append_del(uint64_t seqno, std::string_view k);
  void fsync_if_needed();

private:
  bool append_(const WalRecordMeta &m, std::string_view k, std::string_view v);

  bool write_vectored(const struct iovec *iov, int iovcnt);
  bool fsync_via_backend();

  int fd_ = -1;
  std::string path_;
  size_t bytes_since_sync_ = 0;

  bool use_uring_ = false;
  unsigned uring_qd_ = 256;
  struct UringCtx; // fwd
  UringCtx *uring_ = nullptr;
};

class WalReader {
public:
  explicit WalReader(const std::string &wal_path);
  ~WalReader();
  struct Item {
    uint32_t flags;
    uint64_t seqno;
    std::string key;
    std::string value;
  };
  std::optional<Item> next();
  bool good() const { return fd_ >= 0; }

private:
  int fd_ = -1;
  std::string path_;
};

} // namespace uringkv
