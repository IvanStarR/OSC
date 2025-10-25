#pragma once
#include <string>
#include <vector>
#include <utility>
#include <string_view>
#include <cstdint>
#include <optional>           // ← важно!
#include "sst/record.hpp"     // или "uringkv/sst/record.hpp" если у тебя такой путь

namespace uringkv {

// Пишем отсортированные записи (ключи и tombstone-дeлеты)
class SstWriter {
public:
  explicit SstWriter(const std::string& path);
  ~SstWriter();

  // entries должны быть отсортированы по ключу (возрастание).
  // pair.second.has_value() => PUT; иначе DEL.
  bool write_sorted(
      const std::vector<std::pair<std::string, std::optional<std::string>>>& entries);

private:
  std::string path_;
  int fd_ = -1;
};

} // namespace uringkv
