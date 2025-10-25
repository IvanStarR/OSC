// include/wal/uring_backend.hpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/uio.h> // struct iovec

namespace uringkv {

// Thin wrapper around io_uring (if available). Falls back to POSIX if not initialized.
class UringBackend {
public:
  // Single configurable ctor; extra params have sensible defaults.
  UringBackend(unsigned qd = 256, bool sqpoll = false,
               size_t fixed_buffer_len = 0, unsigned submit_batch = 16);
  ~UringBackend();

  UringBackend(const UringBackend&) = delete;
  UringBackend& operator=(const UringBackend&) = delete;

  UringBackend(UringBackend&&) noexcept;
  UringBackend& operator=(UringBackend&&) noexcept;

  bool initialized() const noexcept;

  bool writev(int fd, const struct ::iovec* iov, int iovcnt);
  bool fsync(int fd);

  // Optional stats (may return 0 if not implemented)
  uint64_t buf_acquires() const noexcept { return 0; }
  uint64_t buf_releases() const noexcept { return 0; }

private:
  struct Impl;
  Impl* p_ = nullptr;
};

} // namespace uringkv
