// source/wal/writer.cpp
#include "wal/writer.hpp"
#include "util.hpp"
#include "wal/record.hpp"
#include "wal/segment.hpp"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef __linux__
#include <fcntl.h>
#include <linux/version.h>
#include <sys/syscall.h>
#endif

namespace uringkv {

static std::vector<std::string> list_wal_segments_sorted(const std::string &dir) {
  std::vector<std::string> out;
  DIR *d = ::opendir(dir.c_str());
  if (!d) return out;
  while (auto *ent = ::readdir(d)) {
    std::string n = ent->d_name;
    if (n.size() == 10 && n.substr(6) == ".wal") {
      bool digits = std::all_of(n.begin(), n.begin()+6,
                                [](unsigned char c){ return std::isdigit(c); });
      if (digits) out.push_back(n);
    }
  }
  ::closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

WalWriter::WalWriter(const std::string &wal_dir, bool use_uring,
                     unsigned uring_qd, bool uring_sqpoll,
                     uint64_t max_segment_bytes, uint64_t group_commit_bytes,
                     FlushMode flush_mode)
    : wal_dir_(wal_dir),
      use_uring_(use_uring),
      uring_(use_uring ? UringBackend(uring_qd, uring_sqpoll) : UringBackend()),
      max_segment_bytes_(max_segment_bytes),
      group_commit_bytes_(group_commit_bytes ? group_commit_bytes : (1ull<<20)),
      flush_mode_(flush_mode) {
  if (wal_dir_.empty()) return;

  if (use_uring_ && !uring_.initialized()) {
    use_uring_ = false;
    spdlog::warn("liburing not available; falling back to POSIX I/O");
  } else if (use_uring_) {
    spdlog::info("io_uring enabled (qd={}, sqpoll={})", uring_qd, uring_sqpoll ? "on" : "off");
  }

  auto files = list_wal_segments_sorted(wal_dir_);
  seg_index_ = files.empty() ? 0 : std::stoull(files.back().substr(0, 6));
  (void)open_new_segment(seg_index_ + 1, /*start_seqno*/ 1);
}

WalWriter::~WalWriter() {
  if (fd_ >= 0) ::close(fd_);
}

WalWriter::WalWriter(WalWriter &&o) noexcept {
  fd_ = o.fd_; o.fd_ = -1;
  wal_dir_ = std::move(o.wal_dir_);
  path_ = std::move(o.path_);
  bytes_since_sync_ = o.bytes_since_sync_;
  uring_ = std::move(o.uring_);
  use_uring_ = o.use_uring_;
  seg_index_ = o.seg_index_;
  seg_size_ = o.seg_size_;
  max_segment_bytes_ = o.max_segment_bytes_;
  group_commit_bytes_ = o.group_commit_bytes_;
  flush_mode_ = o.flush_mode_;
  sync_fsync_ = o.sync_fsync_;
  sync_fdatasync_ = o.sync_fdatasync_;
  sync_sfr_ = o.sync_sfr_;
}

WalWriter &WalWriter::operator=(WalWriter &&o) noexcept {
  if (this != &o) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = o.fd_; o.fd_ = -1;
    wal_dir_ = std::move(o.wal_dir_);
    path_ = std::move(o.path_);
    bytes_since_sync_ = o.bytes_since_sync_;
    uring_ = std::move(o.uring_);
    use_uring_ = o.use_uring_;
    seg_index_ = o.seg_index_;
    seg_size_ = o.seg_size_;
    max_segment_bytes_ = o.max_segment_bytes_;
    group_commit_bytes_ = o.group_commit_bytes_;
    flush_mode_ = o.flush_mode_;
    sync_fsync_ = o.sync_fsync_;
    sync_fdatasync_ = o.sync_fdatasync_;
    sync_sfr_ = o.sync_sfr_;
  }
  return *this;
}

bool WalWriter::open_new_segment(uint64_t index, uint64_t start_seqno) {
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }

  seg_index_ = index;
  path_ = join_path(wal_dir_, wal_segment_name(index));

  fd_ = ::open(path_.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_APPEND, 0600);
  if (fd_ < 0) { spdlog::error("WAL open failed: {}", path_); return false; }
  spdlog::info("WAL open: {}", path_);

  WalSegmentHeader hdr{};
  std::memset(&hdr, 0, sizeof(hdr));
  std::memcpy(hdr.magic, WalSegmentConst::MAGIC, 7);
  hdr.version = WalSegmentConst::VERSION;
  hdr.start_seqno = start_seqno;

  char pad[WalSegmentConst::HEADER_SIZE];
  std::memset(pad, 0, sizeof(pad));
  std::memcpy(pad, &hdr, sizeof(hdr));
  ssize_t w = ::write(fd_, pad, sizeof(pad));
  if (w != (ssize_t)sizeof(pad)) { spdlog::error("WAL write header failed"); return false; }

  seg_size_ = WalSegmentConst::HEADER_SIZE;
  bytes_since_sync_ = 0;
  return true;
}

bool WalWriter::open_or_rotate_if_needed(uint64_t next_bytes, uint64_t next_seqno) {
  if (seg_size_ + next_bytes + sizeof(WalRecordTrailer) > max_segment_bytes_) {
    if (!this->fsync_backend()) return false;
    return open_new_segment(seg_index_ + 1, next_seqno);
  }
  return true;
}

bool WalWriter::append_put(uint64_t seqno, std::string_view k, std::string_view v) {
  WalRecordMeta m{static_cast<uint32_t>(k.size()),
                  static_cast<uint32_t>(v.size()),
                  WAL_FLAG_PUT, seqno, dummy_checksum(k, v)};
  return append_(m, k, v);
}

bool WalWriter::append_del(uint64_t seqno, std::string_view k) {
  std::string_view v{};
  WalRecordMeta m{static_cast<uint32_t>(k.size()), 0u, WAL_FLAG_DEL, seqno, dummy_checksum(k, v)};
  return append_(m, k, v);
}

bool WalWriter::write_vectored(const struct ::iovec *iov, int iovcnt) {
  if (use_uring_ && uring_.initialized()) {
    if (uring_.writev(fd_, iov, iovcnt)) return true;
  }
  ssize_t w = ::writev(fd_, iov, iovcnt);
  if (w < 0) { spdlog::error("writev failed: {}", strerror(errno)); return false; }
  return true;
}

static inline bool posix_fdatasync(int fd) {
#ifdef __linux__
  return ::fdatasync(fd) == 0;
#else
  return ::fsync(fd) == 0;
#endif
}

bool WalWriter::fsync_backend() {
  if (use_uring_ && uring_.initialized()) {
    // через io_uring всегда IORING_FSYNC_DATASYNC
    bool ok = uring_.fsync(fd_);
    if (ok) ++sync_fdatasync_;
    return ok;
  }
  switch (flush_mode_) {
    case FlushMode::FDATASYNC: {
      bool ok = posix_fdatasync(fd_);
      if (ok) ++sync_fdatasync_;
      return ok;
    }
    case FlushMode::FSYNC: {
      bool ok = (::fsync(fd_) == 0);
      if (ok) ++sync_fsync_;
      return ok;
    }
    case FlushMode::SYNC_FILE_RANGE: {
#ifdef __linux__
      off_t end = ::lseek(fd_, 0, SEEK_END);
      if (end < 0) return false;
#ifdef SYNC_FILE_RANGE_WRITE
      int rc = ::sync_file_range(fd_, 0, end, SYNC_FILE_RANGE_WRITE);
      if (rc == 0) { ++sync_sfr_; return true; }
      else return false;
#else
      bool ok = posix_fdatasync(fd_);
      if (ok) ++sync_fdatasync_;
      return ok;
#endif
#else
      bool ok = (::fsync(fd_) == 0);
      if (ok) ++sync_fsync_;
      return ok;
#endif
    }
  }
  bool ok = (::fsync(fd_) == 0);
  if (ok) ++sync_fsync_;
  return ok;
}

bool WalWriter::append_(const WalRecordMeta &m, std::string_view k, std::string_view v) {
  if (fd_ < 0) return false;

  const uint64_t body = sizeof(m) + k.size() + v.size();
  const uint64_t trailer_sz = sizeof(WalRecordTrailer);

  if (!this->open_or_rotate_if_needed(body + trailer_sz, m.seqno)) return false;

  WalRecordTrailer tr{};
  tr.rec_len = static_cast<uint32_t>(body);
  tr.magic   = WAL_TRAILER_MAGIC;

  struct ::iovec iov[4];
  iov[0].iov_base = const_cast<void *>(static_cast<const void *>(&m));
  iov[0].iov_len  = sizeof(m);
  iov[1].iov_base = const_cast<char *>(k.data());
  iov[1].iov_len  = k.size();
  iov[2].iov_base = const_cast<char *>(v.data());
  iov[2].iov_len  = v.size();
  iov[3].iov_base = &tr;
  iov[3].iov_len  = sizeof(tr);

  if (!this->write_vectored(iov, 4)) return false;

  const uint64_t used = body + trailer_sz;
  const uint64_t rem  = used % WalSegmentConst::BLOCK_SIZE;
  uint64_t pad = rem ? (WalSegmentConst::BLOCK_SIZE - rem) : 0;
  if (pad) {
    char zeros[WalSegmentConst::BLOCK_SIZE] = {0};
    while (pad > 0) {
      const size_t chunk = static_cast<size_t>(std::min<uint64_t>(pad, sizeof(zeros)));
      ssize_t w = ::write(fd_, zeros, chunk);
      if (w < 0) { spdlog::error("WAL pad write failed"); return false; }
      pad -= static_cast<uint64_t>(w);
    }
  }

  seg_size_ += used + ((rem) ? (WalSegmentConst::BLOCK_SIZE - rem) : 0);
  bytes_since_sync_ += used;

  if (bytes_since_sync_ >= group_commit_bytes_) {
    if (!this->fsync_backend()) return false;
    bytes_since_sync_ = 0;
  }
  return true;
}

void WalWriter::fsync_if_needed() {
  if (fd_ >= 0) (void)this->fsync_backend();
}

} // namespace uringkv
