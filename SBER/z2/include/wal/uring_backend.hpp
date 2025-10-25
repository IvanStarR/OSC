#pragma once
#include <sys/uio.h>

namespace uringkv {

class UringBackend {
public:
  explicit UringBackend(unsigned qd = 256, bool sqpoll = false);
  ~UringBackend();

  UringBackend(const UringBackend&) = delete;
  UringBackend& operator=(const UringBackend&) = delete;
  UringBackend(UringBackend&&) noexcept;
  UringBackend& operator=(UringBackend&&) noexcept;

  bool initialized() const noexcept;

  bool writev(int fd, const struct ::iovec* iov, int iovcnt);
  bool fsync(int fd);

private:
  struct Impl;
  Impl* p_ = nullptr;
};

} // namespace uringkv
