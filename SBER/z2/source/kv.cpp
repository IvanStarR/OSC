#include "kv.hpp"
#include "sst/manifest.hpp"
#include "sst/reader.hpp"
#include "sst/writer.hpp"
#include "util.hpp"
#include "wal/reader.hpp"
#include "wal/writer.hpp"

#include <algorithm>
#include <atomic>
#include <dirent.h>
#include <filesystem>
#include <mutex>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace uringkv {

struct KV::Impl {
  KVOptions opts;
  std::string wal_dir;
  std::string sst_dir;

  void purge_wal_files_locked() {
    DIR *d = ::opendir(wal_dir.c_str());
    if (!d)
      return;
    while (auto *e = ::readdir(d)) {
      std::string n = e->d_name;
      if (n.size() == 10 && n.substr(6) == ".wal") {
        ::unlink(join_path(wal_dir, n).c_str());
      }
    }
    ::closedir(d);
    // заново открыть свежий сегмент
    wal = WalWriter(wal_dir, opts.use_uring, opts.uring_queue_depth,
                    opts.wal_max_segment_bytes);
  }

  WalWriter wal{std::string{}, false, 256, 64ull * 1024 * 1024};

  // MemTable: key -> optional<value> (nullopt = tombstone)
  std::unordered_map<std::string, std::optional<std::string>> mem;
  uint64_t mem_bytes = 0;

  // Список SST файлов (полные пути), по возрастанию индекса
  std::vector<std::string> ssts;
  uint64_t next_sst_index = 0;

  std::mutex mu;
  std::atomic<uint64_t> seq{1};

  Impl(const KVOptions &o) : opts(o) {
    wal_dir = join_path(opts.path, "wal");
    sst_dir = join_path(opts.path, "sst");
    ensure_dir(opts.path);
    ensure_dir(wal_dir);
    ensure_dir(sst_dir);

    wal = WalWriter(wal_dir, opts.use_uring, opts.uring_queue_depth,
                    opts.wal_max_segment_bytes);

    // Загрузка существующих SST
    ssts.clear();
    for (auto &name : list_sst_sorted(sst_dir)) {
      ssts.push_back(join_path(sst_dir, name));
      uint64_t idx = std::stoull(name.substr(0, 6));
      next_sst_index = std::max(next_sst_index, idx);
    }
    // CURRENT (последний номер)
    uint64_t cur = 0;
    if (read_current(sst_dir, cur))
      next_sst_index = std::max(next_sst_index, cur);
    // Replay WAL
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

  void maybe_flush_locked() {
    if (mem_bytes < opts.sst_flush_threshold_bytes)
      return;

    // Готовим отсортированный вектор (включая tombstone)
    std::vector<std::pair<std::string, std::optional<std::string>>> entries;
    entries.reserve(mem.size());
    for (auto &[k, v] : mem)
      entries.emplace_back(k, v);
    std::sort(entries.begin(), entries.end(),
              [](auto &a, auto &b) { return a.first < b.first; });

    // Имя нового SST
    uint64_t idx = next_sst_index + 1;
    auto name = sst_name(idx);
    auto path = join_path(sst_dir, name);

    SstWriter wr(path);
    if (!wr.write_sorted(entries)) {
      spdlog::error("SST flush failed: {}", path);
      return;
    }
    // Обновим CURRENT и список
    if (!write_current_atomic(sst_dir, idx)) {
      spdlog::warn("Failed to update CURRENT for SST {}", idx);
    }
    next_sst_index = idx;
    ssts.push_back(path);
    purge_wal_files_locked();
    // Очистим MemTable
    mem.clear();
    mem_bytes = 0;

    spdlog::info("Flushed MemTable to {}", path);
  }
};

KV::KV(const KVOptions &opts) : p_(new Impl(opts)) {}
KV::~KV() { delete p_; }

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

  auto &slot = p_->mem[std::string(key)];
  // обновление mem_bytes: сначала вычтем старое
  if (slot.has_value())
    p_->mem_bytes -= (slot->size());
  // новая запись
  slot = std::string(value);
  p_->mem_bytes += key.size() + value.size();

  p_->maybe_flush_locked();
  return true;
}

std::optional<std::string> KV::get(std::string_view key) {
  std::lock_guard lk(p_->mu);
  auto it = p_->mem.find(std::string(key));
  if (it != p_->mem.end()) {
    if (!it->second.has_value())
      return std::nullopt;
    return it->second.value();
  }
  // ищем в SST с конца (самый новый сначала)
  for (auto itf = p_->ssts.rbegin(); itf != p_->ssts.rend(); ++itf) {
    SstReader r(*itf);
    if (!r.good())
      continue;
    auto st = r.get(key);
    if (!st)
      continue;
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

  auto &slot = p_->mem[std::string(key)];
  if (slot.has_value())
    p_->mem_bytes -= slot->size();
  slot = std::nullopt;
  p_->mem_bytes += key.size();

  p_->maybe_flush_locked();
  return true;
}

std::vector<RangeItem> KV::scan(std::string_view start, std::string_view end) {
  std::lock_guard lk(p_->mu);

  // финальное состояние ключей (учитывая mem и все SST; приоритет — свежее)
  std::unordered_map<std::string, std::optional<std::string>> view;

  // 1) mem (самое свежее)
  for (auto &[k, v] : p_->mem)
    view[k] = v;

  // 2) sst с конца к началу — заполняем только то, чего ещё нет в view
  for (auto itf = p_->ssts.rbegin(); itf != p_->ssts.rend(); ++itf) {
    SstReader r(*itf);
    if (!r.good())
      continue;
    auto items = r.scan(start, end);
    // r.scan отдаёт только PUT; но нам нужны и DEL, поэтому пробежимся get'ом
    // для пустых/непопавших
    for (auto &kv : items) {
      if (view.find(kv.first) == view.end())
        view[kv.first] = kv.second;
    }
    // быстрый проход по потенциальным пробелам диапазона: опционально можно
    // пропустить
  }

  // Соберём конечный список в диапазоне
  std::vector<RangeItem> out;
  for (auto &[k, v] : view) {
    if ((!start.empty() && k < start) || (!end.empty() && k > end))
      continue;
    if (v.has_value())
      out.push_back({k, *v});
  }
  std::sort(out.begin(), out.end(),
            [](auto &a, auto &b) { return a.key < b.key; });
  return out;
}

} // namespace uringkv
