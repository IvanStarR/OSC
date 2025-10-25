#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sst/record.hpp"
#include "sst/index.hpp"
#include "sst/footer.hpp"

namespace uringkv {

class SstTable {
public:
  explicit SstTable(std::string path);
  ~SstTable();

  SstTable(const SstTable&) = delete;
  SstTable& operator=(const SstTable&) = delete;

  bool good() const { return fd_ >= 0 && !index_.empty(); }
  const std::string& path() const { return path_; }

  // Возвращает {flag, value} или nullopt (ключ не найден / tombstone).
  std::optional<std::pair<uint32_t, std::string>> get(std::string_view key) const;

private:
  bool load_footer_and_index();
  bool read_record_at(uint64_t off, SstRecordMeta& m, std::string& k, std::string& v) const;

  std::string path_;
  int fd_ = -1;
  SstIndex index_;
};

} // namespace uringkv
