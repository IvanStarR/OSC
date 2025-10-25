#include "wal/uring_backend.hpp"
#include <unistd.h>
#include <cerrno>
#include <cstring>
#ifdef __has_include
#  if __has_include(<liburing.h>)
#    include <liburing.h>
#    define URKV_HAVE_URING 1
#  endif
#endif
#include <spdlog/spdlog.h>

namespace uringkv {

struct UringBackend::Impl {
#if URKV_HAVE_URING
  io_uring ring{};
  bool ok = false;

  // batching
  unsigned pending = 0;
  unsigned submit_batch = 16;

  // fixed files
  bool files_registered = false;
  int  registered_fd    = -1;

  explicit Impl(unsigned qd, bool sqpoll) {
    unsigned flags = 0;
#ifdef IORING_SETUP_SQPOLL
    if (sqpoll) flags |= IORING_SETUP_SQPOLL;
#endif
    if (io_uring_queue_init(qd ? qd : 256, &ring, flags) == 0) ok = true;
    if (ok && (flags & IORING_SETUP_SQPOLL)) {
      spdlog::info("io_uring: SQPOLL enabled");
    }
  }
  ~Impl() {
    if (ok) io_uring_queue_exit(&ring);
  }

  bool ensure_fixed_file(int fd) {
    if (!ok) return false;
    if (!files_registered) {
      int fds[1] = { fd };
      if (io_uring_register_files(&ring, fds, 1) == 0) {
        files_registered = true;
        registered_fd = fd;
        return true;
      }
      return false;
    }
    if (registered_fd != fd) {
      int fds[1] = { fd };
      if (io_uring_register_files_update(&ring, 0, fds, 1) >= 0) {
        registered_fd = fd;
        return true;
      }
      return false;
    }
    return true;
  }
#else
  explicit Impl(unsigned, bool) {}
  ~Impl() = default;
#endif
};

UringBackend::UringBackend(unsigned qd, bool sqpoll) {
#if URKV_HAVE_URING
  p_ = new Impl(qd, sqpoll);
  if (!p_->ok) { delete p_; p_ = nullptr; }
#else
  (void)qd; (void)sqpoll;
  p_ = nullptr;
#endif
}
UringBackend::~UringBackend(){ delete p_; }
UringBackend::UringBackend(UringBackend&& o) noexcept { p_ = o.p_; o.p_ = nullptr; }
UringBackend& UringBackend::operator=(UringBackend&& o) noexcept {
  if (this != &o) { delete p_; p_ = o.p_; o.p_ = nullptr; } return *this;
}
bool UringBackend::initialized() const noexcept { return p_ != nullptr; }

bool UringBackend::writev(int fd, const struct ::iovec* iov, int iovcnt) {
#if URKV_HAVE_URING
  if (!p_ || !p_->ok) return false;

  if (!p_->ensure_fixed_file(fd)) {
    io_uring_sqe* sqe = io_uring_get_sqe(&p_->ring);
    if (!sqe) return false;
    io_uring_prep_writev(sqe, fd, const_cast<struct ::iovec*>(iov), iovcnt, 0);
    int submitted = io_uring_submit(&p_->ring);
    if (submitted < 0) return false;
    io_uring_cqe* cqe = nullptr;
    int ret = io_uring_wait_cqe(&p_->ring, &cqe);
    if (ret < 0) return false;
    bool ok = (cqe->res >= 0);
    io_uring_cqe_seen(&p_->ring, cqe);
    return ok;
  }

  io_uring_sqe* sqe = io_uring_get_sqe(&p_->ring);
  if (!sqe) {
    int s = io_uring_submit(&p_->ring);
    if (s < 0) return false;
    p_->pending = 0;
    sqe = io_uring_get_sqe(&p_->ring);
    if (!sqe) return false;
  }

  io_uring_prep_writev(sqe, /*fixed index*/0, const_cast<struct ::iovec*>(iov), iovcnt, 0);
  sqe->flags |= IOSQE_FIXED_FILE;
  p_->pending++;

  if (p_->pending >= p_->submit_batch) {
    int s = io_uring_submit(&p_->ring);
    if (s < 0) return false;
    for (int i=0;i<s;++i) {
      io_uring_cqe* cqe = nullptr;
      int ret = io_uring_wait_cqe(&p_->ring, &cqe);
      if (ret < 0) return false;
      if (cqe->res < 0) { io_uring_cqe_seen(&p_->ring, cqe); return false; }
      io_uring_cqe_seen(&p_->ring, cqe);
    }
    p_->pending = 0;
  }
  return true;
#else
  (void)fd; (void)iov; (void)iovcnt; return false;
#endif
}

bool UringBackend::fsync(int fd) {
#if URKV_HAVE_URING
  if (!p_ || !p_->ok) return false;

  if (p_->pending > 0) {
    int s = io_uring_submit(&p_->ring);
    if (s < 0) return false;
    for (int i=0;i<s;++i) {
      io_uring_cqe* cqe = nullptr;
      int ret = io_uring_wait_cqe(&p_->ring, &cqe);
      if (ret < 0) return false;
      if (cqe->res < 0) { io_uring_cqe_seen(&p_->ring, cqe); return false; }
      io_uring_cqe_seen(&p_->ring, cqe);
    }
    p_->pending = 0;
  }

  if (!p_->ensure_fixed_file(fd)) {
    io_uring_sqe* sqe = io_uring_get_sqe(&p_->ring);
    if (!sqe) return false;
    io_uring_prep_fsync(sqe, fd, IORING_FSYNC_DATASYNC);
    int s = io_uring_submit(&p_->ring);
    if (s < 0) return false;
    io_uring_cqe* cqe = nullptr;
    int ret = io_uring_wait_cqe(&p_->ring, &cqe);
    if (ret < 0) return false;
    bool ok = (cqe->res >= 0);
    io_uring_cqe_seen(&p_->ring, cqe);
    return ok;
  }

  io_uring_sqe* sqe = io_uring_get_sqe(&p_->ring);
  if (!sqe) return false;
  io_uring_prep_fsync(sqe, /*fixed index*/0, IORING_FSYNC_DATASYNC);
  sqe->flags |= IOSQE_FIXED_FILE;

  int s = io_uring_submit(&p_->ring);
  if (s < 0) return false;

  io_uring_cqe* cqe = nullptr;
  int ret = io_uring_wait_cqe(&p_->ring, &cqe);
  if (ret < 0) return false;
  bool ok = (cqe->res >= 0);
  io_uring_cqe_seen(&p_->ring, cqe);
  return ok;
#else
  (void)fd; return false;
#endif
}

} // namespace uringkv
