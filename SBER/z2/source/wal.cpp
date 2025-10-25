#include "wal.hpp"
#include "util.hpp"
#include <cstring>
#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <utility>

#ifdef __has_include
#if __has_include(<liburing.h>)
#include <liburing.h>
#define URINGKV_HAVE_LIBURING 1
#endif
#endif

namespace uringkv {

static constexpr uint32_t FLAG_PUT = 1u;
static constexpr uint32_t FLAG_DEL = 2u;

struct WalWriter::UringCtx {
#if URINGKV_HAVE_LIBURING
  io_uring ring{};
  bool initialized = false;
  UringCtx(unsigned qd) {
    if (io_uring_queue_init(qd ? qd : 256, &ring, 0) == 0) {
      initialized = true;
    }
  }
  ~UringCtx() {
    if (initialized)
      io_uring_queue_exit(&ring);
  }
#else
  UringCtx(unsigned) {}
#endif
};

WalWriter::WalWriter(const std::string &wal_path, bool use_uring,
                     unsigned uring_qd)
    : path_(wal_path), use_uring_(use_uring), uring_qd_(uring_qd) {
  if (path_.empty())
    return;

  fd_ = ::open(path_.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
  if (fd_ < 0) {
    spdlog::error("WAL open failed: {}", path_);
  } else {
    spdlog::info("WAL open: {}", path_);
  }

#if URINGKV_HAVE_LIBURING
  if (use_uring_) {
    uring_ = new UringCtx(uring_qd_);
    if (!uring_ || !uring_->initialized) {
      spdlog::warn(
          "io_uring init failed or not available; falling back to POSIX I/O");
      use_uring_ = false;
      delete uring_;
      uring_ = nullptr;
    } else {
      spdlog::info("io_uring enabled (qd={})", uring_qd_);
    }
  }
#else
  if (use_uring_) {
    spdlog::warn("liburing headers not found at compile time; falling back to "
                 "POSIX I/O");
    use_uring_ = false;
  }
#endif
}

WalWriter::~WalWriter() {
  if (fd_ >= 0)
    ::close(fd_);
  delete uring_;
  uring_ = nullptr;
}

// move-ctor
WalWriter::WalWriter(WalWriter &&other) noexcept {
  fd_ = other.fd_;
  path_ = std::move(other.path_);
  bytes_since_sync_ = other.bytes_since_sync_;
  use_uring_ = other.use_uring_;
  uring_qd_ = other.uring_qd_;
  uring_ = other.uring_;
  other.fd_ = -1;
  other.bytes_since_sync_ = 0;
  other.use_uring_ = false;
  other.uring_ = nullptr;
}

// move-assign
WalWriter &WalWriter::operator=(WalWriter &&other) noexcept {
  if (this != &other) {
    if (fd_ >= 0)
      ::close(fd_);
    delete uring_;
    fd_ = other.fd_;
    path_ = std::move(other.path_);
    bytes_since_sync_ = other.bytes_since_sync_;
    use_uring_ = other.use_uring_;
    uring_qd_ = other.uring_qd_;
    uring_ = other.uring_;
    other.fd_ = -1;
    other.bytes_since_sync_ = 0;
    other.use_uring_ = false;
    other.uring_ = nullptr;
  }
  return *this;
}

bool WalWriter::append_put(uint64_t seqno, std::string_view k,
                           std::string_view v) {
  WalRecordMeta m{static_cast<uint32_t>(k.size()),
                  static_cast<uint32_t>(v.size()), FLAG_PUT, seqno,
                  dummy_checksum(k, v)};
  return append_(m, k, v);
}

bool WalWriter::append_del(uint64_t seqno, std::string_view k) {
  std::string_view v{};
  WalRecordMeta m{static_cast<uint32_t>(k.size()), 0u, FLAG_DEL, seqno,
                  dummy_checksum(k, v)};
  return append_(m, k, v);
}

bool WalWriter::append_(const WalRecordMeta &m, std::string_view k,
                        std::string_view v) {
  if (fd_ < 0)
    return false;

  struct iovec iov[3];
  iov[0].iov_base = const_cast<WalRecordMeta *>(&m);
  iov[0].iov_len = sizeof(m);
  iov[1].iov_base = const_cast<char *>(k.data());
  iov[1].iov_len = k.size();
  iov[2].iov_base = const_cast<char *>(v.data());
  iov[2].iov_len = v.size();

  if (!write_vectored(iov, 3))
    return false;

  bytes_since_sync_ += sizeof(m) + k.size() + v.size();
  if (bytes_since_sync_ > (1 << 20)) { // ~1MB
    if (!fsync_via_backend())
      return false;
    bytes_since_sync_ = 0;
  }
  return true;
}

bool WalWriter::write_vectored(const struct iovec *iov, int iovcnt) {
#if URINGKV_HAVE_LIBURING
  if (use_uring_ && uring_ && uring_->initialized) {
    io_uring_sqe *sqe = io_uring_get_sqe(&uring_->ring);
    if (!sqe) {
      spdlog::error("io_uring_get_sqe failed");
      return false;
    }
    io_uring_prep_writev(sqe, fd_, iov, iovcnt, 0 /*append via O_APPEND*/);
    io_uring_sqe_set_flags(sqe, 0);
    int ret = io_uring_submit(&uring_->ring);
    if (ret < 0) {
      spdlog::error("io_uring_submit(writev) {}", ret);
      return false;
    }
    io_uring_cqe *cqe = nullptr;
    ret = io_uring_wait_cqe(&uring_->ring, &cqe);
    if (ret < 0) {
      spdlog::error("io_uring_wait_cqe {}", ret);
      return false;
    }
    bool ok = (cqe->res >= 0);
    if (!ok)
      spdlog::error("io_uring writev res={}", cqe->res);
    io_uring_cqe_seen(&uring_->ring, cqe);
    return ok;
  }
#endif
  // fallback POSIX
  ssize_t w = ::writev(fd_, iov, iovcnt);
  if (w < 0) {
    spdlog::error("writev failed: {}", strerror(errno));
    return false;
  }
  return true;
}

bool WalWriter::fsync_via_backend() {
#if URINGKV_HAVE_LIBURING
  if (use_uring_ && uring_ && uring_->initialized) {
    io_uring_sqe *sqe = io_uring_get_sqe(&uring_->ring);
    if (!sqe) {
      spdlog::error("io_uring_get_sqe failed (fsync)");
      return false;
    }
    io_uring_prep_fsync(sqe, fd_, IORING_FSYNC_DATASYNC); // fdatasync
    int ret = io_uring_submit(&uring_->ring);
    if (ret < 0) {
      spdlog::error("io_uring_submit(fsync) {}", ret);
      return false;
    }
    io_uring_cqe *cqe = nullptr;
    ret = io_uring_wait_cqe(&uring_->ring, &cqe);
    if (ret < 0) {
      spdlog::error("io_uring_wait_cqe (fsync) {}", ret);
      return false;
    }
    bool ok = (cqe->res >= 0);
    if (!ok)
      spdlog::error("io_uring fsync res={}", cqe->res);
    io_uring_cqe_seen(&uring_->ring, cqe);
    return ok;
  }
#endif
  return ::fsync(fd_) == 0;
}

void WalWriter::fsync_if_needed() {
  if (fd_ >= 0)
    fsync_via_backend();
}

WalReader::WalReader(const std::string &wal_path) : path_(wal_path) {
  fd_ = ::open(path_.c_str(), O_RDONLY);
}
WalReader::~WalReader() {
  if (fd_ >= 0)
    ::close(fd_);
}

std::optional<WalReader::Item> WalReader::next() {
  if (fd_ < 0)
    return std::nullopt;
  WalRecordMeta m{};
  ssize_t r = ::read(fd_, &m, sizeof(m));
  if (r == 0)
    return std::nullopt; // EOF
  if (r != (ssize_t)sizeof(m))
    return std::nullopt; // truncated
  std::string k;
  k.resize(m.klen);
  if (m.klen && ::read(fd_, k.data(), m.klen) != (ssize_t)m.klen)
    return std::nullopt;
  std::string v;
  v.resize(m.vlen);
  if (m.vlen && ::read(fd_, v.data(), m.vlen) != (ssize_t)m.vlen)
    return std::nullopt;
  if (m.checksum != dummy_checksum(k, v))
    return std::nullopt;
  return Item{m.flags, m.seqno, std::move(k), std::move(v)};
}

} // namespace uringkv
