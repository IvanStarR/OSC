// include/wal/uring_backend.hpp
#pragma once
#include <cstdint>
#include <sys/uio.h>

namespace uringkv {

// Простая обёртка над io_uring с поддержкой:
//  - fixed files
//  - fixed buffers (пул одинаковых блоков)
//  - writev() c автоматическим использованием write_fixed, если влезает
//  - fsync() (IORING_FSYNC_DATASYNC)
//
// Если liburing недоступен — все методы возвращают false, а initialized()==false.

class UringBackend {
public:
  UringBackend(unsigned qd = 256, bool sqpoll = false,
               size_t fixed_buf_size = 256 * 1024,
               unsigned fixed_buf_count = 32);
  ~UringBackend();
  UringBackend(const UringBackend&) = delete;
  UringBackend& operator=(const UringBackend&) = delete;
  UringBackend(UringBackend&&) noexcept;
  UringBackend& operator=(UringBackend&&) noexcept;

  bool initialized() const noexcept;

  // writev: если возможно — склеиваем в fixed buffer и шлём write_fixed,
  // иначе используем прежний путь (writev).
  bool writev(int fd, const struct ::iovec* iov, int iovcnt);

  // fsync датасинк (используется для group-commit)
  bool fsync(int fd);

  // метрики по fixed buffers
  uint64_t buf_acquires() const noexcept;
  uint64_t buf_releases() const noexcept;

private:
  struct Impl;
  Impl* p_ = nullptr;
};

} // namespace uringkv
