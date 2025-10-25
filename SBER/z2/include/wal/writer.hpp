#pragma once
#include <string>
#include <string_view>
#include <sys/uio.h>
#include "kv.hpp"
#include "wal/uring_backend.hpp"

namespace uringkv {

struct WalRecordMeta;
struct WalRecordTrailer;

class WalWriter {
public:
  WalWriter(const std::string &wal_dir,
            bool use_uring,
            unsigned uring_qd,
            bool uring_sqpoll,
            uint64_t max_segment_bytes,
            uint64_t group_commit_bytes,
            FlushMode flush_mode);

  ~WalWriter();

  WalWriter(WalWriter&&) noexcept;
  WalWriter& operator=(WalWriter&&) noexcept;

  bool append_put(uint64_t seqno, std::string_view k, std::string_view v);
  bool append_del(uint64_t seqno, std::string_view k);

  void fsync_if_needed();

private:
  bool open_new_segment(uint64_t index, uint64_t start_seqno);
  bool open_or_rotate_if_needed(uint64_t next_bytes, uint64_t next_seqno);
  bool write_vectored(const struct ::iovec *iov, int iovcnt);
  bool fsync_backend();

  // обязательно объявлен в заголовке
  bool append_(const WalRecordMeta &m, std::string_view k, std::string_view v);

  std::string  wal_dir_;
  std::string  path_;
  int          fd_ = -1;

  bool         use_uring_ = false;
  UringBackend uring_;

  uint64_t     seg_index_ = 0;
  uint64_t     seg_size_  = 0;

  uint64_t     max_segment_bytes_;
  uint64_t     group_commit_bytes_;
  FlushMode    flush_mode_;

  uint64_t     bytes_since_sync_ = 0;
};

} // namespace uringkv
