#include "wal.hpp"
#include "util.hpp"
#include <cstring>
#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace uringkv {

static constexpr uint32_t FLAG_PUT = 1u;
static constexpr uint32_t FLAG_DEL = 2u;

WalWriter::WalWriter(const std::string &wal_path) : path_(wal_path) {
  // Ленивая инициализация: если путь пустой — просто оставляем fd_ = -1
  if (path_.empty())
    return;

  fd_ = ::open(path_.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
  if (fd_ < 0) {
    spdlog::error("WAL open failed: {}", path_);
  } else {
    spdlog::info("WAL open: {}", path_);
  }
}

WalWriter::~WalWriter() {
  if (fd_ >= 0)
    ::close(fd_);
}

// move-ctor
WalWriter::WalWriter(WalWriter &&other) noexcept {
  fd_ = other.fd_;
  path_ = std::move(other.path_);
  bytes_since_sync_ = other.bytes_since_sync_;
  other.fd_ = -1;
  other.bytes_since_sync_ = 0;
}

// move-assign
WalWriter &WalWriter::operator=(WalWriter &&other) noexcept {
  if (this != &other) {
    if (fd_ >= 0)
      ::close(fd_);
    fd_ = other.fd_;
    path_ = std::move(other.path_);
    bytes_since_sync_ = other.bytes_since_sync_;
    other.fd_ = -1;
    other.bytes_since_sync_ = 0;
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
  ssize_t w = ::write(fd_, &m, sizeof(m));
  if (w != (ssize_t)sizeof(m)) {
    spdlog::error("WAL write meta failed");
    return false;
  }
  if (!k.empty()) {
    if (::write(fd_, k.data(), k.size()) != (ssize_t)k.size()) {
      spdlog::error("WAL write key failed");
      return false;
    }
  }
  if (!v.empty()) {
    if (::write(fd_, v.data(), v.size()) != (ssize_t)v.size()) {
      spdlog::error("WAL write value failed");
      return false;
    }
  }
  bytes_since_sync_ += sizeof(m) + k.size() + v.size();
  if (bytes_since_sync_ > (1 << 20)) {
    ::fsync(fd_);
    bytes_since_sync_ = 0;
  }
  return true;
}

void WalWriter::fsync_if_needed() {
  if (fd_ >= 0)
    ::fsync(fd_);
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
