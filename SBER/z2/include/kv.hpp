// include/kv.hpp
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uringkv {

enum class FlushMode;
enum class CompactionPolicy { SIZE_TIERED, LEVELED };

struct RangeItem { std::string key; std::string value; };

struct KVOptions {
  std::string path = "/tmp/uringkv_demo";
  bool        use_uring = false;
  unsigned    uring_queue_depth = 256;
  bool        uring_sqpoll = false;

  uint64_t    wal_max_segment_bytes = 64ull * 1024 * 1024;
  uint64_t    wal_group_commit_bytes = (1ull<<20);
  FlushMode   flush_mode;

  uint64_t    sst_flush_threshold_bytes = 4ull * 1024 * 1024;
  bool        background_compaction = true;
  size_t      l0_compact_threshold = 6;
  size_t      table_cache_capacity = 64;

  bool        final_flush_on_close = true;

  CompactionPolicy compaction_policy = CompactionPolicy::SIZE_TIERED;
};

struct KVMetrics {
  // существующие
  uint64_t puts=0, gets=0, dels=0;
  uint64_t get_hits=0, get_misses=0;
  uint64_t wal_bytes=0;
  uint64_t sst_flushes=0, compactions=0;
  uint64_t table_cache_hits=0, table_cache_misses=0, table_cache_opens=0;
  uint64_t mem_bytes=0, sst_count=0;

  // новые (ит2)
  uint64_t wal_sync_fsync=0, wal_sync_fdatasync=0, wal_sync_sfr=0;
  uint64_t io_fixed_buf_acquires=0, io_fixed_buf_releases=0;
  double   put_latency_ema_us=0.0, get_latency_ema_us=0.0, del_latency_ema_us=0.0;
};

class KV {
public:
  explicit KV(const KVOptions& opts);
  ~KV();

  bool init_storage_layout();

  bool put(std::string_view key, std::string_view value);
  std::optional<std::string> get(std::string_view key);
  bool del(std::string_view key);
  std::vector<RangeItem> scan(std::string_view start, std::string_view end);

  KVMetrics get_metrics() const;
  void      reset_metrics(bool reset_cache_stats);

private:
  struct Impl;
  Impl* p_ = nullptr;
};

} // namespace uringkv
