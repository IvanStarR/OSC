#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <sys/uio.h>

#include "wal/record.hpp"
#include "wal/segment.hpp"
#include "wal/uring_backend.hpp"
#include "kv.hpp" // для FlushMode

namespace uringkv {

class WalWriter {
public:
  WalWriter(const std::string& wal_dir,
            bool use_uring,
            unsigned uring_qd,
            uint64_t max_segment_bytes,
            FlushMode flush_mode = FlushMode::FDATASYNC);

  ~WalWriter();

  WalWriter(WalWriter&&) noexcept;
  WalWriter& operator=(WalWriter&&) noexcept;

  bool append_put(uint64_t seqno, std::string_view k, std::string_view v);
  bool append_del(uint64_t seqno, std::string_view k);

  void fsync_if_needed();

private:
  bool open_new_segment(uint64_t index, uint64_t start_seqno);
  bool open_or_rotate_if_needed(uint64_t next_bytes, uint64_t next_seqno);

  bool append_(const WalRecordMeta& m, std::string_view k, std::string_view v);

  bool write_vectored(const struct ::iovec* iov, int iovcnt);
  bool fsync_backend(); // учитывает flush_mode_

private:
  std::string wal_dir_;
  std::string path_;
  int         fd_ = -1;

  UringBackend uring_;
  bool         use_uring_ = false;

  uint64_t seg_index_ = 0;
  uint64_t seg_size_  = 0;
  uint64_t max_segment_bytes_ = 64ull * 1024 * 1024;

  uint64_t bytes_since_sync_ = 0;

  FlushMode flush_mode_ = FlushMode::FDATASYNC;
};

} // namespace uringkv
