#pragma once
#include <string>
#include <optional>
#include <vector>
#include <string_view>
#include "sst/record.hpp"

namespace uringkv {

class SstReader {
public:
  explicit SstReader(const std::string& path);
  ~SstReader();

  bool good() const { return fd_ >= 0; }

  std::optional<std::pair<uint32_t, std::string>> get(std::string_view key);

  std::vector<std::pair<std::string,std::string>> scan(std::string_view start, std::string_view end);

private:
  std::string path_;
  int fd_ = -1;
};

} // namespace uringkv
