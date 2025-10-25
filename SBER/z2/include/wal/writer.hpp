#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

#include "kv.hpp"                 // FlushMode
#include "wal/uring_backend.hpp"  // UringBackend

namespace uringkv {

struct WalRecordMeta;
struct WalRecordTrailer;

class WalWriter {
public:
  // NEW: добавлены параметры fixed_buffer_bytes и submit_batch
  WalWriter(const std::string& wal_dir, bool use_uring,
            unsigned uring_qd, bool uring_sqpoll,
            std::size_t uring_fixed_buffer_bytes, unsigned uring_submit_batch,
            uint64_t max_segment_bytes, uint64_t group_commit_bytes,
            FlushMode flush_mode);

  ~WalWriter();

  WalWriter(WalWriter&&) noexcept;
  WalWriter& operator=(WalWriter&&) noexcept;

  bool append_put(uint64_t seqno, std::string_view k, std::string_view v);
  bool append_del(uint64_t seqno, std::string_view k);

  // принудительный fsync по политике
  void fsync_if_needed();

  // useful for checking constructed state
  bool good() const noexcept { return fd_ >= 0; }

private:
  bool open_new_segment(uint64_t index, uint64_t start_seqno);
  bool open_or_rotate_if_needed(uint64_t next_bytes, uint64_t next_seqno);
  bool write_vectored(const struct ::iovec* iov, int iovcnt);
  bool fsync_backend();
  bool append_(const WalRecordMeta& m, std::string_view k, std::string_view v);

private:
  std::string wal_dir_;
  std::string path_;

  bool         use_uring_ = false;
  UringBackend uring_;

  int      fd_ = -1;
  uint64_t seg_index_ = 0;
  uint64_t seg_size_  = 0;

  uint64_t bytes_since_sync_ = 0;

  uint64_t max_segment_bytes_   = 64ull * 1024 * 1024;
  uint64_t group_commit_bytes_  = (1ull<<20);
  FlushMode flush_mode_         = FlushMode::FDATASYNC;

  // метрики durability (счётчики вызовов)
  uint64_t sync_fsync_      = 0;
  uint64_t sync_fdatasync_  = 0;
  uint64_t sync_sfr_        = 0;
};

} // namespace uringkv
