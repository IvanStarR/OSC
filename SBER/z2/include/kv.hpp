#pragma once
#include <string>
#include <optional>
#include <vector>
#include <string_view>
#include <cstdint>

namespace uringkv {

struct KVOptions {
  std::string path = "./data";      // база каталога
  bool        use_uring = false;    // опционально io_uring для WAL
  unsigned    uring_queue_depth = 256;
  uint64_t    wal_max_segment_bytes = 64ull * 1024 * 1024; // РАЗМЕР СЕГМЕНТА WAL
};

struct RangeItem {
  std::string key;
  std::string value;
};

class KV {
public:
  explicit KV(const KVOptions& opts);
  ~KV();

  KV(const KV&) = delete; 
  KV& operator=(const KV&) = delete;

  bool put(std::string_view key, std::string_view value);
  std::optional<std::string> get(std::string_view key);
  bool del(std::string_view key);
  std::vector<RangeItem> scan(std::string_view start, std::string_view end);

  bool init_storage_layout();

private:
  struct Impl; 
  Impl* p_;
};

} // namespace uringkv
