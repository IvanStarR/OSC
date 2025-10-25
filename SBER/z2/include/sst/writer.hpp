#pragma once
#include <string>
#include <optional>
#include <vector>
#include <string_view>
#include <cstdint> 
namespace uringkv {

class SstWriter {
public:
  explicit SstWriter(const std::string& path);
  ~SstWriter();

  SstWriter(const SstWriter&) = delete;
  SstWriter& operator=(const SstWriter&) = delete;

  // entries: vector of (key, value|tombstone)
  bool write_sorted(
      const std::vector<std::pair<std::string, std::optional<std::string>>>& entries,
      uint32_t index_step = 64);

private:
  std::string path_;
  int fd_ = -1;
};

} // namespace uringkv
