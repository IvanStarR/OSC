#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uringkv {

// ----- общие enums -----
enum class FlushMode {
  FDATASYNC,
  FSYNC,
  SYNC_FILE_RANGE
};

enum class CompactionPolicy {
  SIZE_TIERED,
  LEVELED
};

// ----- внешние метрики -----
struct KVMetrics {
  uint64_t puts        = 0;
  uint64_t gets        = 0;
  uint64_t dels        = 0;

  uint64_t get_hits    = 0;
  uint64_t get_misses  = 0;

  uint64_t wal_bytes   = 0;

  uint64_t sst_flushes = 0;
  uint64_t compactions = 0;

  uint64_t table_cache_hits   = 0;
  uint64_t table_cache_misses = 0;
  uint64_t table_cache_opens  = 0;

  uint64_t mem_bytes = 0;
  uint64_t sst_count = 0;
};

// ----- опции -----
struct KVOptions {
  std::string path = "/tmp/uringkv_demo";

  // io_uring
  bool     use_uring           = false;
  unsigned uring_queue_depth   = 256;
  bool     uring_sqpoll        = false;

  // NEW: включение fixed buffers и размер батча submit
  std::size_t uring_fixed_buffer_bytes = 0; // 0 = не использовать fixed buffers
  unsigned    uring_submit_batch       = 16;

  // WAL/SST/flush
  uint64_t   wal_max_segment_bytes     = 64ull * 1024 * 1024;
  uint64_t   wal_group_commit_bytes    = (1ull<<20); // полезные байты до fsync
  uint64_t   sst_flush_threshold_bytes = 4ull * 1024 * 1024;
  FlushMode  flush_mode                = FlushMode::FDATASYNC;

  // компактация/кэш
  bool               background_compaction = true;
  std::size_t        l0_compact_threshold  = 6;
  std::size_t        table_cache_capacity  = 64;
  CompactionPolicy   compaction_policy     = CompactionPolicy::SIZE_TIERED;

  // завершение
  bool final_flush_on_close = true;
};

// ----- SCAN item -----
struct RangeItem {
  std::string key;
  std::string value;
};

// ----- основной класс -----
class KV {
public:
  explicit KV(const KVOptions& opts);
  ~KV();

  // создать директории path/wal и path/sst
  bool init_storage_layout();

  // CRUD
  bool put(std::string_view key, std::string_view value);
  std::optional<std::string> get(std::string_view key);
  bool del(std::string_view key);

  // диапазон
  std::vector<RangeItem> scan(std::string_view start, std::string_view end);

  // метрики
  KVMetrics get_metrics() const;
  void reset_metrics(bool reset_cache_stats);

private:
  struct Impl;
  Impl* p_ = nullptr;
};

} // namespace uringkv
