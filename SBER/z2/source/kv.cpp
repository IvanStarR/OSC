// source/kv.cpp
#include "kv.hpp"
#include "sst/manifest.hpp"
#include "sst/reader.hpp"
#include "sst/writer.hpp"
#include "util.hpp"
#include "wal/reader.hpp"
#include "wal/writer.hpp"
#include "cache/table_cache.hpp"   // используем твой TableCache

#include <algorithm>
#include <atomic>
#include <dirent.h>
#include <filesystem>
#include <mutex>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>

namespace uringkv {

// Сколько L0-файлов допускаем до компактации
static constexpr size_t kL0CompactThreshold = 6;

struct KV::Impl {
  KVOptions opts;
  std::string wal_dir;
  std::string sst_dir;

  WalWriter wal{std::string{}, false, 256, 64ull * 1024 * 1024};

  // Горячая память: MemTable (ключ -> значение / tombstone)
  std::unordered_map<std::string, std::optional<std::string>> mem;
  uint64_t mem_bytes = 0;

  // Диск: отсортированные SST-файлы (полные пути), по возрастанию индекса
  std::vector<std::string> ssts;
  uint64_t next_sst_index = 0;

  // Кэш открытых SST (LRU)
  TableCache tcache{64};

  std::mutex mu;
  std::atomic<uint64_t> seq{1};

  // Удалить все *.wal и переоткрыть новый сегмент
  void purge_wal_files_locked() {
    if (DIR* d = ::opendir(wal_dir.c_str())) {
      while (auto* e = ::readdir(d)) {
        const char* name = e->d_name;
        if (!name || name[0] == '.') continue; // "." и ".."
        std::string n{name};
        if (n.size() == 10 && n.substr(6) == ".wal") {
          ::unlink(join_path(wal_dir, n).c_str());
        }
      }
      ::closedir(d);
    }
    // fsync каталога, чтобы зафиксировать unlink’и
    int dfd = ::open(wal_dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) { (void)::fsync(dfd); ::close(dfd); }

    // Новый пустой сегмент
    wal = WalWriter(wal_dir, opts.use_uring, opts.uring_queue_depth,
                    opts.wal_max_segment_bytes);
  }

  // Компактация L0: сливаем все текущие SST в один файл
  bool compact_l0_locked() {
    if (ssts.size() < kL0CompactThreshold) return true;

    spdlog::info("Compaction: merging {} SST files", ssts.size());
    std::unordered_map<std::string, std::optional<std::string>> view;
    view.reserve(1024);

    // От старых к новым: новые перекрывают старые
    for (const auto& path : ssts) {
      SstReader r(path);
      if (!r.good()) continue;
      auto items = r.scan(std::string_view{}, std::string_view{});
      for (auto& kv : items) {
        if (view.find(kv.first) != view.end()) continue; // уже перекрыто более новым
        view.emplace(kv.first, kv.second);
      }
    }

    std::vector<std::pair<std::string, std::optional<std::string>>> entries;
    entries.reserve(view.size());
    for (auto& [k, v] : view) {
      if (v.has_value()) entries.emplace_back(k, v); // tombstone выбрасываем
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    uint64_t idx  = next_sst_index + 1;
    auto name     = sst_name(idx);
    auto out_path = join_path(sst_dir, name);

    SstWriter wr(out_path);
    if (!wr.write_sorted(entries)) {
      spdlog::error("Compaction failed to write {}", out_path);
      return false;
    }

    if (!write_current_atomic(sst_dir, idx)) {
      spdlog::warn("Compaction: failed to update CURRENT to {}", idx);
    }
    next_sst_index = idx;

    // Удаляем старые SST
    for (const auto& p : ssts) { (void)::unlink(p.c_str()); }
    ssts.clear();
    ssts.push_back(out_path);

    // Сбрасываем кэш таблиц, чтобы не держать дескрипторы удалённых файлов
    tcache = TableCache(opts.table_cache_capacity ? opts.table_cache_capacity : 64);

    spdlog::info("Compaction: done -> {}", out_path);
    return true;
  }

  // Флаш по порогу
  void maybe_flush_locked() {
    if (mem_bytes < opts.sst_flush_threshold_bytes) return;

    std::vector<std::pair<std::string, std::optional<std::string>>> entries;
    entries.reserve(mem.size());
    for (auto& [k,v] : mem) entries.emplace_back(k, v);
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b){ return a.first < b.first; });

    uint64_t idx = next_sst_index + 1;
    auto name = sst_name(idx);
    auto path = join_path(sst_dir, name);

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

    purge_wal_files_locked();
    mem.clear();
    mem_bytes = 0;

    spdlog::info("Flushed MemTable to {}", path);

    // Запускаем компактацию при переполнении
    (void)compact_l0_locked();
  }

  // Полный принудительный флаш (для деструктора при opts.final_flush_on_close)
  void flush_all_locked() {
    if (!mem.empty()) {
      std::vector<std::pair<std::string, std::optional<std::string>>> entries;
      entries.reserve(mem.size());
      for (auto& [k,v] : mem) entries.emplace_back(k, v);
      std::sort(entries.begin(), entries.end(),
                [](const auto& a, const auto& b){ return a.first < b.first; });

      uint64_t idx = next_sst_index + 1;
      auto name = sst_name(idx);
      auto path = join_path(sst_dir, name);

      SstWriter wr(path);
      if (!wr.write_sorted(entries)) {
        spdlog::error("SST final flush failed: {}", path);
        return;
      }
      if (!write_current_atomic(sst_dir, idx)) {
        spdlog::warn("Failed to update CURRENT for SST {}", idx);
      }
      next_sst_index = idx;
      ssts.push_back(path);

      spdlog::info("Flushed MemTable to {} (final)", path);
    }

    // После финального флаша попробуем компактнуть, если файлов много
    (void)compact_l0_locked();

    purge_wal_files_locked();
    mem.clear();
    mem_bytes = 0;
  }

  // Загрузка состояния при старте
  Impl(const KVOptions& o) : opts(o) {
    wal_dir = join_path(opts.path, "wal");
    sst_dir = join_path(opts.path, "sst");
    ensure_dir(opts.path);
    ensure_dir(wal_dir);
    ensure_dir(sst_dir);

    // инициализируем TableCache из опций
    tcache = TableCache(opts.table_cache_capacity ? opts.table_cache_capacity : 64);

    wal = WalWriter(wal_dir, opts.use_uring, opts.uring_queue_depth,
                    opts.wal_max_segment_bytes);

    // Загрузка списка SST
    ssts.clear();
    for (auto& name : list_sst_sorted(sst_dir)) {
      ssts.push_back(join_path(sst_dir, name));
      uint64_t idx = std::stoull(name.substr(0, 6));
      next_sst_index = std::max(next_sst_index, idx);
    }
    uint64_t cur = 0;
    if (read_current(sst_dir, cur))
      next_sst_index = std::max(next_sst_index, cur);

    // Replay WAL → MemTable
    WalReader rd(wal_dir);
    if (rd.good()) {
      size_t replayed = 0;
      while (auto it = rd.next()) {
        if (it->flags == 1) {
          mem[it->key] = it->value;
          mem_bytes += it->key.size() + it->value.size();
        } else if (it->flags == 2) {
          mem[it->key] = std::nullopt;
          mem_bytes += it->key.size();
        }
        seq.store(std::max<uint64_t>(seq.load(), it->seqno + 1));
        ++replayed;
      }
      spdlog::info("Replayed {} WAL records", replayed);
    }
  }
};

KV::KV(const KVOptions& opts) : p_(new Impl(opts)) {}

KV::~KV() {
  if (p_) {
    std::lock_guard lk(p_->mu);
    if (p_->opts.final_flush_on_close) {
      p_->flush_all_locked();
    }
    delete p_;
    p_ = nullptr;
  }
}

bool KV::init_storage_layout() {
  bool ok = ensure_dir(p_->opts.path) &&
            ensure_dir(join_path(p_->opts.path, "wal")) &&
            ensure_dir(join_path(p_->opts.path, "sst"));
  if (ok)
    spdlog::info("Initialized storage layout at {}", p_->opts.path);
  return ok;
}

bool KV::put(std::string_view key, std::string_view value) {
  std::lock_guard lk(p_->mu);
  uint64_t s = p_->seq.fetch_add(1);
  if (!p_->wal.append_put(s, key, value))
    return false;

  auto& slot = p_->mem[std::string(key)];
  if (slot.has_value())
    p_->mem_bytes -= slot->size();
  slot = std::string(value);
  p_->mem_bytes += key.size() + value.size();

  p_->maybe_flush_locked();
  return true;
}

std::optional<std::string> KV::get(std::string_view key) {
  std::lock_guard lk(p_->mu);

  // 1) сперва MemTable
  auto it = p_->mem.find(std::string(key));
  if (it != p_->mem.end()) {
    if (!it->second.has_value())
      return std::nullopt;
    return it->second.value();
  }

  // 2) затем SST с конца (новейшие) — через TableCache
  for (auto itf = p_->ssts.rbegin(); itf != p_->ssts.rend(); ++itf) {
    auto tbl = p_->tcache.get_table(*itf);
    if (!tbl) continue;                // не удалось открыть/прочитать индекс
    auto st = tbl->get(key);
    if (!st) continue;
    if (st->first == SST_FLAG_DEL)
      return std::nullopt;
    return st->second;
  }
  return std::nullopt;
}

bool KV::del(std::string_view key) {
  std::lock_guard lk(p_->mu);
  uint64_t s = p_->seq.fetch_add(1);
  if (!p_->wal.append_del(s, key))
    return false;

  auto& slot = p_->mem[std::string(key)];
  if (slot.has_value())
    p_->mem_bytes -= slot->size();
  slot = std::nullopt;
  p_->mem_bytes += key.size();

  p_->maybe_flush_locked();
  return true;
}

std::vector<RangeItem> KV::scan(std::string_view start, std::string_view end) {
  std::lock_guard lk(p_->mu);

  // итоговое состояние ключей (учёт MemTable и всех SST; приоритет — свежее)
  std::unordered_map<std::string, std::optional<std::string>> view;

  // 1) mem
  for (auto& [k, v] : p_->mem) view[k] = v;

  // 2) sst с конца к началу — дополняем только отсутствующие
  for (auto itf = p_->ssts.rbegin(); itf != p_->ssts.rend(); ++itf) {
    SstReader r(*itf);
    if (!r.good()) continue;
    auto items = r.scan(start, end);
    for (auto& kv : items) {
      if (view.find(kv.first) == view.end())
        view[kv.first] = kv.second;
    }
  }

  // Соберём итоговый список
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

} // namespace uringkv
