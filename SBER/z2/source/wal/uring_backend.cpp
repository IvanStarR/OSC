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

  // batching
  unsigned pending = 0;
  unsigned submit_batch = 16;

  // fixed files
  bool     files_registered = false;
  int      registered_fd    = -1;    // last registered fd stored at index 0

  explicit Impl(unsigned qd) {
    if (io_uring_queue_init(qd ? qd : 256, &ring, 0) == 0) ok = true;
  }
  ~Impl(){
    if (ok) {
      io_uring_queue_exit(&ring);
    }
  }
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

#if URKV_HAVE_URING
static bool ensure_fixed_file(UringBackend::Impl* p, int fd) {
  if (!p->ok) return false;
  if (!p->files_registered) {
    int fds[1] = { fd };
    if (io_uring_register_files(&p->ring, fds, 1) == 0) {
      p->files_registered = true;
      p->registered_fd = fd;
      return true;
    }
    return false;
  }
  if (p->registered_fd != fd) {
    int fds[1] = { fd };
    if (io_uring_register_files_update(&p->ring, 0, fds, 1) >= 0) {
      p->registered_fd = fd;
      return true;
    }
    return false;
  }
  return true;
}
#endif

bool UringBackend::writev(int fd, const struct ::iovec* iov, int iovcnt) {
#if URKV_HAVE_URING
  if (!p_ || !p_->ok) return false;

  if (!ensure_fixed_file(p_, fd)) {
    // fallback: обычный путь через submit без fixed files
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

  // batched submit with fixed file
  io_uring_sqe* sqe = io_uring_get_sqe(&p_->ring);
  if (!sqe) {
    // flush pending and retry once
    int s = io_uring_submit(&p_->ring);
    if (s < 0) return false;
    p_->pending = 0;
    sqe = io_uring_get_sqe(&p_->ring);
    if (!sqe) return false;
  }

  io_uring_prep_writev(sqe, /*fd*/0, const_cast<struct ::iovec*>(iov), iovcnt, 0);
  sqe->flags |= IOSQE_FIXED_FILE;

  p_->pending++;

  if (p_->pending >= p_->submit_batch) {
    int s = io_uring_submit(&p_->ring);
    if (s < 0) return false;
    // wait for all just submitted
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
  (void)fd; (void)iov; (void)iovcnt;
  return false;
#endif
}

bool UringBackend::fsync(int fd) {
#if URKV_HAVE_URING
  if (!p_ || !p_->ok) return false;

  // перед fsync — принудительно сабмитим все накопленные SQE и ждём
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

  if (!ensure_fixed_file(p_, fd)) {
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
  io_uring_prep_fsync(sqe, /*fd*/0, IORING_FSYNC_DATASYNC);
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
  (void)fd;
  return false;
#endif
}

} // namespace uringkv
