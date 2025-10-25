#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace uringkv {

// Режимы долговечности
enum class FlushMode : uint8_t {
  FDATASYNC, // по умолчанию
  FSYNC,
  SYNC_FILE_RANGE // sync_file_range(SFR) + периодический fdatasync
};

// Политика компакции (поддерживаем size-tiered; leveled — заглушка до итерации 4)
enum class CompactionPolicy : uint8_t { SIZE_TIERED, LEVELED };

struct KVOptions {
  std::string path = "./data";

  // io_uring
  bool use_uring = false;
  unsigned uring_queue_depth = 256;
  bool uring_sqpoll = false;      // добавлено для полноты, можно оставить false

  // WAL/SST
  uint64_t wal_max_segment_bytes = 64ull * 1024 * 1024;
  uint64_t wal_group_commit_bytes = (1ull<<20); // ~1MiB полезных данных
  uint64_t sst_flush_threshold_bytes = 4ull * 1024 * 1024;
  bool final_flush_on_close = true;

  // Табличный кэш
  size_t table_cache_capacity = 64;

  // Компактация
  bool background_compaction = true;
  size_t l0_compact_threshold = 6;
  CompactionPolicy compaction_policy = CompactionPolicy::SIZE_TIERED;

  // Долговечность
  FlushMode flush_mode = FlushMode::FDATASYNC;
};

struct RangeItem {
  std::string key;
  std::string value;
};

// Плоский снимок метрик (без атомиков — безопасно возвращать наружу)
struct KVMetrics {
  uint64_t puts = 0;
  uint64_t gets = 0;
  uint64_t dels = 0;
  uint64_t get_hits = 0;
  uint64_t get_misses = 0;

  uint64_t wal_bytes = 0;     // суммарно записано в WAL (с трейлером и паддингом)
  uint64_t sst_flushes = 0;   // сколько раз MemTable ушёл в SST
  uint64_t compactions = 0;   // сколько раз сработала L0-компактация

  uint64_t table_cache_hits = 0;
  uint64_t table_cache_misses = 0;
  uint64_t table_cache_opens = 0;

  uint64_t mem_bytes = 0;     // текущее количество байтов в MemTable
  uint64_t sst_count = 0;     // текущее число SST-файлов (L0)
};

class KV {
public:
  explicit KV(const KVOptions &opts);
  ~KV();

  KV(const KV &) = delete;
  KV &operator=(const KV &) = delete;

  bool init_storage_layout();

  bool put(std::string_view key, std::string_view value);
  std::optional<std::string> get(std::string_view key);
  bool del(std::string_view key);
  std::vector<RangeItem> scan(std::string_view start, std::string_view end);

  // Метрики
  KVMetrics get_metrics() const;                 // захватывает снимок
  void reset_metrics(bool reset_cache_stats);    // обнуляет счётчики; при true также обнуляет статистику кэша

private:
  struct Impl;
  Impl *p_;
};

} // namespace uringkv
