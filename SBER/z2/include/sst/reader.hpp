// include/sst/reader.hpp
#pragma once
#include "sst/index.hpp"
#include "sst/record.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uringkv {

// В этой версии Reader поддерживает:
//  - mmap-хеш-индекс для point GET
//  - разрежённый ordered-индекс для ускорения SCAN (lower_bound(start))

class SstReader {
public:
  explicit SstReader(const std::string &path);
  ~SstReader();

  bool good() const { return fd_ >= 0; }

  std::optional<std::pair<uint32_t, std::string>> get(std::string_view key);
  std::vector<std::pair<std::string, std::optional<std::string>>> scan(std::string_view start, std::string_view end);

private:
  bool load_footer_and_index();
  bool read_record_at(uint64_t off, SstRecordMeta &m, std::string &k,
                      std::string &v);

  // sparse index helpers
  bool
  load_sparse_into(std::vector<std::pair<std::string, uint64_t>> &out) const;
  uint64_t find_scan_start_offset(std::string_view start) const;

  std::string path_;
  int fd_ = -1;

  // point-lookup индекс (mmap)
  MmapHashIndex index_;

  // разрежённый индекс
  uint64_t sparse_off_ = 0;
  uint32_t sparse_cnt_ = 0;

  // граница данных (начало индекса) берём из футера при загрузке
  uint64_t data_end_off_ = 0;
};

} // namespace uringkv
