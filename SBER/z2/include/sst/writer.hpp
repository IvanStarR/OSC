#pragma once
#include <string>
#include <vector>
#include <utility>
#include <string_view>
#include <cstdint>
#include <optional>
#include "sst/record.hpp"
#include "sst/index.hpp"

namespace uringkv {

class SstWriter {
public:
  explicit SstWriter(const std::string& path);
  ~SstWriter();

  // entries должны быть отсортированы по ключу (возрастание).
  bool write_sorted(
      const std::vector<std::pair<std::string, std::optional<std::string>>>& entries,
      uint32_t index_step = 32); // каждые N записей добавляем точку индекса

private:
  std::string path_;
  int fd_ = -1;
};

} // namespace uringkv
