#pragma once
#include <string>
#include <string_view>
#include <cstdint>
#include <sys/uio.h>
#include "wal/record.hpp"
#include "wal/segment.hpp"
#include "wal/uring_backend.hpp"
#include <fcntl.h> 

namespace uringkv {

class WalWriter {
public:
  explicit WalWriter(const std::string& wal_dir,
                     bool use_uring = false,
                     unsigned uring_qd = 256,
                     uint64_t max_segment_bytes = 64ull * 1024 * 1024);
  ~WalWriter();

  WalWriter(const WalWriter&) = delete;
  WalWriter& operator=(const WalWriter&) = delete;
  WalWriter(WalWriter&&) noexcept;
  WalWriter& operator=(WalWriter&&) noexcept;

  bool append_put(uint64_t seqno, std::string_view k, std::string_view v);
  bool append_del(uint64_t seqno, std::string_view k);
  void fsync_if_needed();

  uint64_t current_segment_index() const { return seg_index_; }

private:
  bool append_(const WalRecordMeta& m, std::string_view k, std::string_view v);
  bool write_vectored(const struct ::iovec* iov, int iovcnt);
  bool fsync_backend();

  bool open_or_rotate_if_needed(uint64_t next_bytes, uint64_t next_seqno);
  bool open_new_segment(uint64_t index, uint64_t start_seqno);

  int fd_ = -1;
  std::string wal_dir_;
  std::string path_;
  size_t bytes_since_sync_ = 0;

  UringBackend uring_;        // если не инициализировался — будет пустым
  bool use_uring_ = false;

  uint64_t seg_index_ = 1;
  uint64_t seg_size_  = 0;    // включая заголовок
  uint64_t max_segment_bytes_ = 64ull * 1024 * 1024;
};

} // namespace uringkv
