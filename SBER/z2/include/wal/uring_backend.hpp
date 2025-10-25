#pragma once
#include <cstdint>

struct iovec;

namespace uringkv {

class UringBackend {
public:
  UringBackend() = default;
  explicit UringBackend(unsigned qd);
  ~UringBackend();

  UringBackend(const UringBackend&) = delete;
  UringBackend& operator=(const UringBackend&) = delete;
  UringBackend(UringBackend&&) noexcept;
  UringBackend& operator=(UringBackend&&) noexcept;

  bool initialized() const noexcept;
  bool writev(int fd, const struct ::iovec* iov, int iovcnt);
  bool fsync(int fd);

private:
  struct Impl; Impl* p_ = nullptr;
};

} // namespace uringkv
