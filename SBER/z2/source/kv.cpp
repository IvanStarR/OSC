#include "kv.hpp"
#include "util.hpp"
#include "wal.hpp"
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <spdlog/spdlog.h>
#include <unordered_map>

namespace uringkv {

struct KV::Impl {
  KVOptions opts;
  std::string wal_path;
  WalWriter wal{std::string{}, false, 256}; // пустой, ленивый
  std::unordered_map<std::string, std::optional<std::string>>
      mem; // tombstone = nullopt
  std::mutex mu;
  std::atomic<uint64_t> seq{1};

  Impl(const KVOptions &o) : opts(o) {
    auto wal_dir = join_path(opts.path, "wal");
    ensure_dir(opts.path);
    ensure_dir(wal_dir);
    wal_path = join_path(wal_dir, "000001.wal");
    // создаём WAL с учётом io_uring-флагов
    wal = WalWriter(wal_path, opts.use_uring, opts.uring_queue_depth);

    // Recovery (MVP): replay WAL
    WalReader rd(wal_path);
    if (rd.good()) {
      size_t replayed = 0;
      while (auto it = rd.next()) {
        if (it->flags == 1)
          mem[it->key] = it->value; // PUT
        else if (it->flags == 2)
          mem[it->key] = std::nullopt; // DEL
        seq.store(std::max<uint64_t>(seq.load(), it->seqno + 1));
        ++replayed;
      }
      spdlog::info("Replayed {} WAL records", replayed);
    }
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
  p_->mem[std::string(key)] = std::string(value);
  return true;
}

std::optional<std::string> KV::get(std::string_view key) {
  std::lock_guard lk(p_->mu);
  auto it = p_->mem.find(std::string(key));
  if (it == p_->mem.end())
    return std::nullopt;
  if (!it->second.has_value())
    return std::nullopt;
  return it->second.value();
}

bool KV::del(std::string_view key) {
  std::lock_guard lk(p_->mu);
  uint64_t s = p_->seq.fetch_add(1);
  if (!p_->wal.append_del(s, key))
    return false;
  p_->mem[std::string(key)] = std::nullopt;
  return true;
}

std::vector<RangeItem> KV::scan(std::string_view start, std::string_view end) {
  std::lock_guard lk(p_->mu);
  std::vector<RangeItem> out;
  std::vector<std::string> keys;
  keys.reserve(p_->mem.size());
  for (auto &[k, v] : p_->mem)
    if (v.has_value())
      keys.push_back(k);
  std::sort(keys.begin(), keys.end());
  for (auto &k : keys) {
    if ((!start.empty() && k < start) || (!end.empty() && k > end))
      continue;
    out.push_back(RangeItem{k, p_->mem[k].value()});
  }
  return out;
}

} // namespace uringkv
