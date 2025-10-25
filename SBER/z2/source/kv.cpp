#include "kv.hpp"
#include "sst/manifest.hpp"
#include "sst/reader.hpp"
#include "sst/writer.hpp"
#include "util.hpp"
#include "wal/reader.hpp"
#include "wal/writer.hpp"
#include "cache/table_cache.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <dirent.h>
#include <filesystem>
#include <mutex>
#include <spdlog/spdlog.h>
#include <thread>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>

namespace uringkv {

struct KV::Impl {
  KVOptions    opts;
  std::string  wal_dir;
  std::string  sst_dir;

  // WAL: пустая инициализация; реально пересоздаём по opts
  WalWriter wal{std::string{}, false, 256, false,
                64ull * 1024 * 1024, (1ull<<20), FlushMode::FDATASYNC};

  // MemTable: key -> value / tombstone
  std::unordered_map<std::string, std::optional<std::string>> mem;
  uint64_t mem_bytes = 0;

  // L0 SST list (full paths), ascending by index
  std::vector<std::string> ssts;
  uint64_t next_sst_index = 0;

  // LRU-кэш таблиц
  TableCache tcache{64};

  // Фоновая компактация
  std::thread             bg_compactor;
  std::condition_variable cv;
  bool                    need_compact = false;
  bool                    stopping     = false;

  std::mutex              mu;
  std::atomic<uint64_t>   seq{1};

  // ---- метрики (внутренние атомики) ----
  std::atomic<uint64_t> m_puts{0}, m_gets{0}, m_dels{0};
  std::atomic<uint64_t> m_get_hits{0}, m_get_misses{0};
  std::atomic<uint64_t> m_wal_bytes{0};
  std::atomic<uint64_t> m_sst_flushes{0};
  std::atomic<uint64_t> m_compactions{0};

  // ---- helpers ----

  // Подсчёт точного объёма записи WAL (мета+key+value+трейлер+паддинг)
  static uint64_t wal_bytes_for_record(size_t klen, size_t vlen) {
    const uint64_t meta = sizeof(WalRecordMeta) + klen + vlen;
    const uint64_t trailer = sizeof(WalRecordTrailer);
    const uint64_t used = meta + trailer;
    const uint64_t rem  = used % WalSegmentConst::BLOCK_SIZE;
    const uint64_t pad  = rem ? (WalSegmentConst::BLOCK_SIZE - rem) : 0;
    return used + pad;
  }

  void purge_wal_files_locked() {
    // удалить все *.wal
    if (DIR* d = ::opendir(wal_dir.c_str())) {
      while (auto* e = ::readdir(d)) {
        const char* name = e->d_name;
        if (!name || name[0] == '.') continue;
        std::string n{name};
        if (n.size() == 10 && n.substr(6) == ".wal") {
          ::unlink(join_path(wal_dir, n).c_str());
        }
      }
      ::closedir(d);
    }
    // fsync каталога wal
    int dfd = ::open(wal_dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) { (void)::fsync(dfd); ::close(dfd); }

    // открыть новый WAL с актуальными опциями
    wal = WalWriter(wal_dir,
                    opts.use_uring,
                    opts.uring_queue_depth,
                    opts.uring_sqpoll,
                    opts.wal_max_segment_bytes,
                    opts.wal_group_commit_bytes,
                    opts.flush_mode);
  }

  // Одна итерация L0-компактации
  bool compact_l0_once() {
    // Шаг 1: снимок входа под локом
    std::vector<std::string> input;
    uint64_t new_idx = 0;
    {
      std::unique_lock<std::mutex> lk(mu);
      if (stopping) return true;
      if (ssts.size() < opts.l0_compact_threshold) return true;
      input   = ssts;
      new_idx = next_sst_index + 1;
      next_sst_index = new_idx; // бронируем имя
    }

    spdlog::info("BG-Compaction: merging {} SST files", input.size());

    // Шаг 2: финальный view — идём с конца, чтобы новые перекрывали старые
    std::unordered_map<std::string, std::optional<std::string>> view;
    view.reserve(1024);

    for (auto it = input.rbegin(); it != input.rend(); ++it) {
      SstReader r(*it);
      if (!r.good()) continue;
      auto items = r.scan(std::string_view{}, std::string_view{});
      for (auto& kv : items) {
        if (view.find(kv.first) != view.end()) continue;
        view.emplace(kv.first, kv.second);
      }
    }

    std::vector<std::pair<std::string, std::optional<std::string>>> entries;
    entries.reserve(view.size());
    for (auto& [k, v] : view)
      if (v.has_value()) entries.emplace_back(k, v);
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    // Шаг 3: пишем новый SST
    const auto out_name = sst_name(new_idx);
    const auto out_path = join_path(sst_dir, out_name);

    SstWriter wr(out_path);
    if (!wr.write_sorted(entries)) {
      spdlog::error("BG-Compaction failed to write {}", out_path);
      return false;
    }

    // Шаг 4: коммит под локом
    {
      std::unique_lock<std::mutex> lk(mu);
      if (stopping) return true;

      std::vector<std::string> new_list;
      new_list.reserve(ssts.size()+1);
      for (auto& p : ssts) {
        if (std::find(input.begin(), input.end(), p) == input.end())
          new_list.push_back(p);
      }
      new_list.push_back(out_path);
      ssts.swap(new_list);

      if (!write_current_atomic(sst_dir, new_idx)) {
        spdlog::warn("BG-Compaction: failed CURRENT -> {}", new_idx);
      }
      for (const auto& p : input) { (void)::unlink(p.c_str()); }

      tcache = TableCache(opts.table_cache_capacity ? opts.table_cache_capacity : 64);
      m_compactions.fetch_add(1, std::memory_order_relaxed);
    }

    spdlog::info("BG-Compaction: done -> {}", out_path);
    return true;
  }

  void maybe_schedule_compaction_locked() {
    if (!opts.background_compaction) return;
    if (ssts.size() >= opts.l0_compact_threshold) {
      need_compact = true;
      cv.notify_one();
    }
  }

  void maybe_flush_locked() {
    if (mem_bytes < opts.sst_flush_threshold_bytes) return;

    std::vector<std::pair<std::string, std::optional<std::string>>> entries;
    entries.reserve(mem.size());
    for (auto& [k,v] : mem) entries.emplace_back(k, v);
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    const uint64_t idx  = next_sst_index + 1;
    const auto     name = sst_name(idx);
    const auto     path = join_path(sst_dir, name);

    SstWriter wr(path);
    if (!wr.write_sorted(entries)) {
      spdlog::error("SST flush failed: {}", path);
      return;
    }
    if (!write_current_atomic(sst_dir, idx)) {
      spdlog::warn("Failed to update CURRENT for SST {}", idx);
    }
    next_sst_index = idx;
    ssts.push_back(path);

    // purge WAL и очистка MemTable
    purge_wal_files_locked();
    mem.clear();
    mem_bytes = 0;

    m_sst_flushes.fetch_add(1, std::memory_order_relaxed);
    spdlog::info("Flushed MemTable to {}", path);

    maybe_schedule_compaction_locked();
  }

  void flush_all_locked() {
    if (!mem.empty()) {
      std::vector<std::pair<std::string, std::optional<std::string>>> entries;
      entries.reserve(mem.size());
      for (auto& [k,v] : mem) entries.emplace_back(k, v);
      std::sort(entries.begin(), entries.end(),
                [](const auto& a, const auto& b){ return a.first < b.first; });

      const uint64_t idx  = next_sst_index + 1;
      const auto     name = sst_name(idx);
      const auto     path = join_path(sst_dir, name);

      SstWriter wr(path);
      if (!wr.write_sorted(entries)) {
        spdlog::error("SST final flush failed: {}", path);
      } else {
        if (!write_current_atomic(sst_dir, idx)) {
          spdlog::warn("Failed to update CURRENT for SST {}", idx);
        }
        next_sst_index = idx;
        ssts.push_back(path);
        m_sst_flushes.fetch_add(1, std::memory_order_relaxed);
        spdlog::info("Flushed MemTable to {} (final)", path);
      }
    }

    purge_wal_files_locked();
    mem.clear();
    mem_bytes = 0;
  }

  void compactor_thread() {
    std::unique_lock<std::mutex> lk(mu);
    while (true) {
      cv.wait_for(lk, std::chrono::milliseconds(200),
                  [&]{ return stopping || need_compact; });
      if (stopping) break;
      if (!need_compact) continue;
      need_compact = false;

      lk.unlock();
      (void)compact_l0_once();
      lk.lock();
    }
  }

  // Корректно остановить фон, если он есть
  void stop_bg_if_any() {
    if (!opts.background_compaction) return;
    {
      std::lock_guard<std::mutex> lk(mu);
      stopping = true;
      need_compact = false;
    }
    cv.notify_all();
    if (bg_compactor.joinable()) bg_compactor.join();
  }

  // init
  Impl(const KVOptions& o) : opts(o) {
    wal_dir = join_path(opts.path, "wal");
    sst_dir = join_path(opts.path, "sst");
    ensure_dir(opts.path);
    ensure_dir(wal_dir);
    ensure_dir(sst_dir);

    tcache = TableCache(opts.table_cache_capacity ? opts.table_cache_capacity : 64);

    // Создаём WAL по opts
    wal = WalWriter(wal_dir,
                    opts.use_uring,
                    opts.uring_queue_depth,
                    opts.uring_sqpoll,
                    opts.wal_max_segment_bytes,
                    opts.wal_group_commit_bytes,
                    opts.flush_mode);

    // Прочитать SST и вычислить next_sst_index
    ssts.clear();
    for (auto& name : list_sst_sorted(sst_dir)) {
      ssts.push_back(join_path(sst_dir, name));
      const uint64_t idx = std::stoull(name.substr(0, 6));
      next_sst_index = std::max(next_sst_index, idx);
    }
    uint64_t cur = 0;
    if (read_current(sst_dir, cur))
      next_sst_index = std::max(next_sst_index, cur);

    // WAL replay
    WalReader rd(wal_dir);
    if (rd.good()) {
      size_t replayed = 0;
      while (auto it = rd.next()) {
        if (it->flags == WAL_FLAG_PUT) {
          mem[it->key] = it->value;
          mem_bytes += it->key.size() + it->value.size();
        } else if (it->flags == WAL_FLAG_DEL) {
          mem[it->key] = std::nullopt;
          mem_bytes += it->key.size();
        }
        seq.store(std::max<uint64_t>(seq.load(), it->seqno + 1));
        ++replayed;
      }
      spdlog::info("Replayed {} WAL records", replayed);
    }

    if (opts.background_compaction) {
      bg_compactor = std::thread([this]{ compactor_thread(); });
    }
  }

  ~Impl() { stop_bg_if_any(); }
};

// ===== KV API =====

KV::KV(const KVOptions& opts) : p_(new Impl(opts)) {}

KV::~KV() {
  if (!p_) return;

  // остановить фон
  p_->stop_bg_if_any();

  // финальный flush под локом (опционально)
  {
    std::lock_guard lk(p_->mu);
    if (p_->opts.final_flush_on_close) {
      p_->flush_all_locked();
    }
  }

  // при выключенной фоновой компактации — один проход уже БЕЗ лока
  if (!p_->opts.background_compaction) {
    (void)p_->compact_l0_once();
  }

  delete p_;
  p_ = nullptr;
}

bool KV::init_storage_layout() {
  bool ok = ensure_dir(p_->opts.path) &&
            ensure_dir(join_path(p_->opts.path, "wal")) &&
            ensure_dir(join_path(p_->opts.path, "sst"));
  if (ok) spdlog::info("Initialized storage layout at {}", p_->opts.path);
  return ok;
}

bool KV::put(std::string_view key, std::string_view value) {
  std::lock_guard lk(p_->mu);
  const uint64_t s = p_->seq.fetch_add(1);
  if (!p_->wal.append_put(s, key, value)) return false;

  auto& slot = p_->mem[std::string(key)];
  if (slot.has_value()) p_->mem_bytes -= slot->size();
  slot = std::string(value);
  p_->mem_bytes += key.size() + value.size();

  // метрики
  p_->m_puts.fetch_add(1, std::memory_order_relaxed);
  p_->m_wal_bytes.fetch_add(Impl::wal_bytes_for_record(key.size(), value.size()),
                             std::memory_order_relaxed);

  p_->maybe_flush_locked();
  return true;
}

std::optional<std::string> KV::get(std::string_view key) {
  std::lock_guard lk(p_->mu);
  p_->m_gets.fetch_add(1, std::memory_order_relaxed);

  auto it = p_->mem.find(std::string(key));
  if (it != p_->mem.end()) {
    if (!it->second.has_value()) { p_->m_get_misses.fetch_add(1, std::memory_order_relaxed); return std::nullopt; }
    p_->m_get_hits.fetch_add(1, std::memory_order_relaxed);
    return it->second.value();
  }

  for (auto itf = p_->ssts.rbegin(); itf != p_->ssts.rend(); ++itf) {
    auto tbl = p_->tcache.get_table(*itf);
    if (!tbl) continue;
    auto st = tbl->get(key);
    if (!st) continue;
    if (st->first == SST_FLAG_DEL) { p_->m_get_misses.fetch_add(1, std::memory_order_relaxed); return std::nullopt; }
    p_->m_get_hits.fetch_add(1, std::memory_order_relaxed);
    return st->second;
  }
  p_->m_get_misses.fetch_add(1, std::memory_order_relaxed);
  return std::nullopt;
}

bool KV::del(std::string_view key) {
  std::lock_guard lk(p_->mu);
  const uint64_t s = p_->seq.fetch_add(1);
  if (!p_->wal.append_del(s, key)) return false;

  auto& slot = p_->mem[std::string(key)];
  if (slot.has_value()) p_->mem_bytes -= slot->size();
  slot = std::nullopt;
  p_->mem_bytes += key.size();

  // метрики
  p_->m_dels.fetch_add(1, std::memory_order_relaxed);
  p_->m_wal_bytes.fetch_add(Impl::wal_bytes_for_record(key.size(), 0),
                             std::memory_order_relaxed);

  p_->maybe_flush_locked();
  return true;
}

std::vector<RangeItem> KV::scan(std::string_view start, std::string_view end) {
  std::lock_guard lk(p_->mu);

  std::unordered_map<std::string, std::optional<std::string>> view;
  for (auto& [k, v] : p_->mem) view[k] = v;

  for (auto itf = p_->ssts.rbegin(); itf != p_->ssts.rend(); ++itf) {
    SstReader r(*itf);
    if (!r.good()) continue;
    auto items = r.scan(start, end);
    for (auto& kv : items) {
      if (view.find(kv.first) == view.end())
        view[kv.first] = kv.second;
    }
  }

  std::vector<RangeItem> out;
  out.reserve(view.size());
  for (auto& [k, v] : view) {
    if ((!start.empty() && k < start) || (!end.empty() && k > end)) continue;
    if (v.has_value()) out.push_back({k, *v});
  }
  std::sort(out.begin(), out.end(),
            [](const auto& a, const auto& b){ return a.key < b.key; });
  return out;
}

// -------- Метрики: API --------

KVMetrics KV::get_metrics() const {
  std::lock_guard lk(p_->mu);
  KVMetrics m;
  m.puts        = p_->m_puts.load(std::memory_order_relaxed);
  m.gets        = p_->m_gets.load(std::memory_order_relaxed);
  m.dels        = p_->m_dels.load(std::memory_order_relaxed);
  m.get_hits    = p_->m_get_hits.load(std::memory_order_relaxed);
  m.get_misses  = p_->m_get_misses.load(std::memory_order_relaxed);
  m.wal_bytes   = p_->m_wal_bytes.load(std::memory_order_relaxed);
  m.sst_flushes = p_->m_sst_flushes.load(std::memory_order_relaxed);
  m.compactions = p_->m_compactions.load(std::memory_order_relaxed);

  m.table_cache_hits   = p_->tcache.hits();
  m.table_cache_misses = p_->tcache.misses();
  m.table_cache_opens  = p_->tcache.opens();

  m.mem_bytes = p_->mem_bytes;
  m.sst_count = static_cast<uint64_t>(p_->ssts.size());
  return m;
}

void KV::reset_metrics(bool reset_cache_stats) {
  std::lock_guard lk(p_->mu);
  p_->m_puts.store(0, std::memory_order_relaxed);
  p_->m_gets.store(0, std::memory_order_relaxed);
  p_->m_dels.store(0, std::memory_order_relaxed);
  p_->m_get_hits.store(0, std::memory_order_relaxed);
  p_->m_get_misses.store(0, std::memory_order_relaxed);
  p_->m_wal_bytes.store(0, std::memory_order_relaxed);
  p_->m_sst_flushes.store(0, std::memory_order_relaxed);
  p_->m_compactions.store(0, std::memory_order_relaxed);
  if (reset_cache_stats) p_->tcache.reset_stats();
}

} // namespace uringkv
