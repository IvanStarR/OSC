#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace uringkv {

struct WalRecordMeta {
  uint32_t klen;
  uint32_t vlen;
  uint32_t flags; // 1=PUT, 2=DEL
  uint64_t seqno;
  uint64_t checksum; // placeholder; will swap to xxhash later
};

class WalWriter {
public:
  explicit WalWriter(const std::string &wal_path);
  ~WalWriter();

  // non-copyable, but movable
  WalWriter(const WalWriter &) = delete;
  WalWriter &operator=(const WalWriter &) = delete;
  WalWriter(WalWriter &&other) noexcept;
  WalWriter &operator=(WalWriter &&other) noexcept;

  bool append_put(uint64_t seqno, std::string_view k, std::string_view v);
  bool append_del(uint64_t seqno, std::string_view k);
  void fsync_if_needed();

private:
  bool append_(const WalRecordMeta &m, std::string_view k, std::string_view v);
  int fd_ = -1;
  std::string path_;
  size_t bytes_since_sync_ = 0;
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
