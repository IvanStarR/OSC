#include "wal/reader.hpp"
#include "util.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>
#include <algorithm>

namespace uringkv {

static std::vector<std::string> list_wal_segments_sorted(const std::string& dir) {
  std::vector<std::string> out;
  DIR* d = ::opendir(dir.c_str());
  if (!d) return out;
  while (auto* ent = ::readdir(d)) {
    std::string n = ent->d_name;
    if (n.size() == 10 && n.substr(6) == ".wal") {
      bool digits = std::all_of(n.begin(), n.begin()+6, ::isdigit);
      if (digits) out.push_back(n);
    }
  }
  ::closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

WalReader::WalReader(const std::string& wal_dir) : wal_dir_(wal_dir) {
  files_ = list_wal_segments_sorted(wal_dir_);
  file_pos_ = 0;
  fd_ = -1;
  (void)open_next_file();
}

WalReader::~WalReader(){ if(fd_>=0) ::close(fd_); }

bool WalReader::read_segment_header(int fd) {
  char pad[WalSegmentConst::HEADER_SIZE];
  ssize_t r = ::read(fd, pad, sizeof(pad));
  if (r == 0) return false;
  if (r != (ssize_t)sizeof(pad)) return false;
  WalSegmentHeader hdr{};
  std::memcpy(&hdr, pad, sizeof(hdr));
  if (std::memcmp(hdr.magic, WalSegmentConst::MAGIC, 7) != 0) return false;
  if (hdr.version != WalSegmentConst::VERSION) return false;
  return true;
}

bool WalReader::open_next_file() {
  if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
  while (file_pos_ < files_.size()) {
    auto p = join_path(wal_dir_, files_[file_pos_++]);
    int fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) continue;
    if (!read_segment_header(fd)) { ::close(fd); continue; }
    fd_ = fd;
    return true;
  }
  return false;
}

std::optional<WalReader::Item> WalReader::next() {
  if (fd_ < 0) return std::nullopt;

  WalRecordMeta m{};
  ssize_t r = ::read(fd_, &m, sizeof(m));
  if (r == 0) {
    if (open_next_file()) return next();
    return std::nullopt;
  }
  if (r != (ssize_t)sizeof(m)) {
    if (open_next_file()) return next();
    return std::nullopt;
  }

  std::string k; k.resize(m.klen);
  if (m.klen && ::read(fd_, k.data(), m.klen) != (ssize_t)m.klen) {
    if (open_next_file()) return next();
    return std::nullopt;
  }
  std::string v; v.resize(m.vlen);
  if (m.vlen && ::read(fd_, v.data(), m.vlen) != (ssize_t)m.vlen) {
    if (open_next_file()) return next();
    return std::nullopt;
  }

  // checksum
  extern uint64_t dummy_checksum(std::string_view, std::string_view);
  if (m.checksum != dummy_checksum(k, v)) {
    if (open_next_file()) return next();
    return std::nullopt;
  }

  return Item{m.flags, m.seqno, std::move(k), std::move(v)};
}

} // namespace uringkv
