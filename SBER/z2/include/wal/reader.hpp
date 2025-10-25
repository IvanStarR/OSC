#pragma once
#include <string>
#include <optional>
#include <vector>
#include "wal/record.hpp"
#include "wal/segment.hpp"

namespace uringkv {

class WalReader {
public:
  explicit WalReader(const std::string& wal_dir);
  ~WalReader();

  struct Item { uint32_t flags; uint64_t seqno; std::string key; std::string value; };
  std::optional<Item> next();
  bool good() const { return !files_.empty(); }

private:
  bool open_next_file();
  bool read_segment_header(int fd);

  std::string wal_dir_;
  std::vector<std::string> files_;
  size_t file_pos_ = 0;
  int fd_ = -1;
};

} // namespace uringkv
