#pragma once
#include <string>
#include <optional>
#include <vector>
#include <string_view>
#include "sst/record.hpp"
#include "sst/index.hpp"

namespace uringkv {

class SstReader {
public:
  explicit SstReader(const std::string& path);
  ~SstReader();

  // Достаточно того, что файл открыт. Индекс может отсутствовать.
  bool good() const { return fd_ >= 0; }

  std::optional<std::pair<uint32_t, std::string>> get(std::string_view key);
  std::vector<std::pair<std::string,std::string>> scan(std::string_view start, std::string_view end);

private:
  bool load_footer_and_index();
  bool read_record_at(uint64_t off, SstRecordMeta& m, std::string& k, std::string& v);

  std::string path_;
  int fd_ = -1;
  MmapHashIndex index_;  // если не замапился — сканируем без него
};

} // namespace uringkv
