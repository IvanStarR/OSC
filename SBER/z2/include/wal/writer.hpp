// include/wal/writer.hpp
#pragma once
#include <string>
#include <string_view>
#include <sys/uio.h>
#include <cstdint>

#include "wal/record.hpp"
#include "wal/segment.hpp"
#include "wal/uring_backend.hpp"

namespace uringkv {

enum class FlushMode { FDATASYNC, FSYNC, SYNC_FILE_RANGE };

class WalWriter {
public:
  WalWriter(const std::string &wal_dir, bool use_uring,
            unsigned uring_qd, bool uring_sqpoll,
            uint64_t max_segment_bytes, uint64_t group_commit_bytes,
            FlushMode flush_mode);
  ~WalWriter();
  WalWriter(WalWriter&&) noexcept;
  WalWriter& operator=(WalWriter&&) noexcept;

  bool append_put(uint64_t seqno, std::string_view k, std::string_view v);
  bool append_del(uint64_t seqno, std::string_view k);

  void fsync_if_needed();

  // метрики по синкам
  uint64_t sync_fsync()      const noexcept { return sync_fsync_; }
  uint64_t sync_fdatasync()  const noexcept { return sync_fdatasync_; }
  uint64_t sync_sfr()        const noexcept { return sync_sfr_; }

  // проксирующие метрики пула fixed buffers
  uint64_t io_fixed_buf_acquires() const noexcept { return use_uring_ ? uring_.buf_acquires() : 0; }
  uint64_t io_fixed_buf_releases() const noexcept { return use_uring_ ? uring_.buf_releases() : 0; }

private:
  bool open_new_segment(uint64_t index, uint64_t start_seqno);
  bool open_or_rotate_if_needed(uint64_t next_bytes, uint64_t next_seqno);
  bool append_(const WalRecordMeta &m, std::string_view k, std::string_view v);

  bool write_vectored(const struct ::iovec *iov, int iovcnt);
  bool fsync_backend();

private:
  std::string wal_dir_;
  std::string path_;
  int         fd_ = -1;

  bool        use_uring_;
  UringBackend uring_;

  uint64_t seg_index_ = 0;
  uint64_t seg_size_  = 0;
  uint64_t max_segment_bytes_;
  uint64_t group_commit_bytes_;
  uint64_t bytes_since_sync_ = 0;

  FlushMode flush_mode_;

  // счётчики синков
  uint64_t sync_fsync_ = 0;
  uint64_t sync_fdatasync_ = 0;
  uint64_t sync_sfr_ = 0;
};

} // namespace uringkv
