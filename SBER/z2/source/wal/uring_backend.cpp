#include "wal/uring_backend.hpp"
#include <sys/uio.h>
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
  explicit Impl(unsigned qd) {
    if (io_uring_queue_init(qd ? qd : 256, &ring, 0) == 0) ok = true;
  }
  ~Impl(){ if (ok) io_uring_queue_exit(&ring); }
#else
  explicit Impl(unsigned) {}
  ~Impl() = default;
#endif
};

UringBackend::UringBackend(unsigned qd) {
#if URKV_HAVE_URING
  p_ = new Impl(qd);
  if (!p_->ok) { delete p_; p_ = nullptr; }
#else
  (void)qd;
  p_ = nullptr;
#endif
}

UringBackend::~UringBackend(){ delete p_; }

UringBackend::UringBackend(UringBackend&& o) noexcept { p_ = o.p_; o.p_ = nullptr; }
UringBackend& UringBackend::operator=(UringBackend&& o) noexcept {
  if (this != &o) { delete p_; p_ = o.p_; o.p_ = nullptr; }
  return *this;
}

bool UringBackend::initialized() const noexcept { return p_ != nullptr; }

bool UringBackend::writev(int fd, const struct ::iovec* iov, int iovcnt) {
#if URKV_HAVE_URING
  if (!p_ || !p_->ok) return false;
  io_uring_sqe* sqe = io_uring_get_sqe(&p_->ring);
  if (!sqe) return false;
  io_uring_prep_writev(sqe, fd, const_cast<struct ::iovec*>(iov), iovcnt, 0);
  int ret = io_uring_submit(&p_->ring);
  if (ret < 0) return false;
  io_uring_cqe* cqe = nullptr;
  ret = io_uring_wait_cqe(&p_->ring, &cqe);
  if (ret < 0) return false;
  bool ok = (cqe->res >= 0);
  io_uring_cqe_seen(&p_->ring, cqe);
  return ok;
#else
  (void)fd; (void)iov; (void)iovcnt;
  return false;
#endif
}

bool UringBackend::fsync(int fd) {
#if URKV_HAVE_URING
  if (!p_ || !p_->ok) return false;
  io_uring_sqe* sqe = io_uring_get_sqe(&p_->ring);
  if (!sqe) return false;
  io_uring_prep_fsync(sqe, fd, IORING_FSYNC_DATASYNC);
  int ret = io_uring_submit(&p_->ring);
  if (ret < 0) return false;
  io_uring_cqe* cqe = nullptr;
  ret = io_uring_wait_cqe(&p_->ring, &cqe);
  if (ret < 0) return false;
  bool ok = (cqe->res >= 0);
  io_uring_cqe_seen(&p_->ring, cqe);
  return ok;
#else
  (void)fd;
  return false;
#endif
}

} // namespace uringkv
