#pragma once
#include <string>
#include <vector>
#include <utility>
#include <string_view>
#include <cstdint>
#include "sst/record.hpp"

namespace uringkv {

class SstWriter {
public:
  explicit SstWriter(const std::string& path);
  ~SstWriter();

  bool write_sorted(const std::vector<std::pair<std::string, std::optional<std::string>>>& entries);

private:
  std::string path_;
  int fd_ = -1;
};

} // namespace uringkv
