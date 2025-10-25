#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uringkv {

struct KVOptions {
  std::string path = "./data";
  bool        use_uring = false;
  unsigned    uring_queue_depth = 256;
  uint64_t    wal_max_segment_bytes = 64ull * 1024 * 1024;
  uint64_t    sst_flush_threshold_bytes = 4ull * 1024 * 1024;
  bool        final_flush_on_close = true;

  // Кэш таблиц (если используете TableCache)
  size_t      table_cache_capacity = 64;

  // --- новое: фоновая компактация по ТЗ ---
  bool        background_compaction = true; // компактер в отдельном потоке (ТРЕБОВАНИЕ ТЗ)
  size_t      l0_compact_threshold  = 6;    // порог кол-ва SST в L0 для старта компактации
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

  bool init_storage_layout();

  bool put(std::string_view key, std::string_view value);
  std::optional<std::string> get(std::string_view key);
  bool del(std::string_view key);
  std::vector<RangeItem> scan(std::string_view start, std::string_view end);

private:
  struct Impl;
  Impl* p_;
};

} // namespace uringkv
