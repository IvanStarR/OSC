// tests/iter1_smoke.cpp
#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include "sst/manifest.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <optional>
#include <unistd.h> // getpid()
#include <spdlog/spdlog.h>

using namespace uringkv;
namespace fs = std::filesystem;

// ----------------- helpers -----------------
static std::string mktemp_dir(const char* prefix) {
  auto base = fs::temp_directory_path();
  for (int i=0;i<1000;++i) {
    auto p = base / (std::string(prefix) + std::to_string(::getpid()) + "_" + std::to_string(i));
    std::error_code ec;
    if (fs::create_directories(p, ec)) return p.string();
  }
  auto p = base / (std::string(prefix) + "fallback_" + std::to_string(::getpid()));
  std::error_code ec;
  fs::create_directories(p, ec);
  return p.string();
}

static std::vector<fs::path> list_wals(const fs::path& wal_dir){
  std::vector<fs::path> out;
  std::error_code ec;
  if (!fs::exists(wal_dir, ec)) return out;
  for (auto& e : fs::directory_iterator(wal_dir, ec)){
    if (ec) break;
    if (e.is_regular_file() && e.path().extension()==".wal") out.push_back(e.path());
  }
  return out;
}

// ========================= ITERATION 1 TESTS ==============================

TEST_CASE("iter1: init storage layout") {
  spdlog::set_level(spdlog::level::warn);

  const auto dir = mktemp_dir("uringkv_init_");

  // держим KV в отдельном scope, чтобы деструктор отработал ДО файловых проверок
  {
    KVOptions opts;
    opts.path = dir;
    opts.use_uring = false;
    opts.uring_queue_depth = 256;
    opts.wal_max_segment_bytes = 64ull * 1024 * 1024;
    opts.sst_flush_threshold_bytes = 4ull * 1024 * 1024;
    opts.final_flush_on_close = false;   // ничего не флашим в этом smoke
    opts.table_cache_capacity = 64;
    opts.background_compaction = false;  // итерация 1 — без фона
    opts.l0_compact_threshold = 6;

    KV kv(opts);
    REQUIRE(kv.init_storage_layout());
  }

  REQUIRE(fs::exists(dir));
  REQUIRE(fs::exists(fs::path(dir)/"wal"));
  REQUIRE(fs::exists(fs::path(dir)/"sst"));
}

TEST_CASE("iter1: basic put/get/del") {
  spdlog::set_level(spdlog::level::warn);

  const auto dir = mktemp_dir("uringkv_basic_");

  {
    KVOptions opts;
    opts.path = dir;
    opts.use_uring = false;
    opts.sst_flush_threshold_bytes = 4ull * 1024 * 1024;
    opts.final_flush_on_close = false;   // деструктор не делает тяжёлый flush
    opts.background_compaction = false;

    KV kv(opts);
    REQUIRE(kv.init_storage_layout());

    REQUIRE(kv.put("a","1"));
    REQUIRE(kv.put("b","2"));
    REQUIRE(kv.put("a","3"));               // overwrite

    auto va = kv.get("a");
    auto vb = kv.get("b");
    REQUIRE(va.has_value());
    REQUIRE(vb.has_value());
    REQUIRE(*va == std::string("3"));
    REQUIRE(*vb == std::string("2"));

    REQUIRE(kv.del("a"));
    REQUIRE_FALSE(kv.get("a").has_value()); // tombstone
  }
}

TEST_CASE("iter1: WAL replay persistence (no SST flush)") {
  spdlog::set_level(spdlog::level::warn);

  const auto dir = mktemp_dir("uringkv_replay_");

  // шаг 1: пишем в WAL, НО НЕ флашим MemTable в SST
  {
    KVOptions opts;
    opts.path = dir;
    opts.use_uring = false;
    opts.sst_flush_threshold_bytes = (1ull<<30); // заведомо недостижимый порог
    opts.final_flush_on_close = false;           // важно: оставить в WAL
    opts.background_compaction = false;

    KV kv(opts);
    REQUIRE(kv.init_storage_layout());
    REQUIRE(kv.put("x","1"));
    REQUIRE(kv.put("y","2"));
    REQUIRE(kv.del("x"));
  } // здесь только закрывается WAL, MemTable не уходит в SST

  // шаг 2: переоткрываем и проверяем replay
  {
    KVOptions opts;
    opts.path = dir;
    opts.use_uring = false;
    opts.final_flush_on_close = false;
    opts.background_compaction = false;

    KV kv(opts);
    REQUIRE_FALSE(kv.get("x").has_value()); // tombstone из WAL
    auto vy = kv.get("y");
    REQUIRE(vy.has_value());
    REQUIRE(*vy == std::string("2"));
  }
}

TEST_CASE("iter1: MemTable flush creates SST and purges WAL") {
  spdlog::set_level(spdlog::level::warn);

  const auto dir     = mktemp_dir("uringkv_flush_");
  const auto wal_dir = fs::path(dir) / "wal";
  const auto sst_dir = fs::path(dir) / "sst";

  {
    KVOptions opts;
    opts.path = dir;
    opts.use_uring = false;
    opts.sst_flush_threshold_bytes = 2 * 1024; // низкий порог — гарантированный flush
    opts.final_flush_on_close = true;
    opts.background_compaction = false;

    KV kv(opts);
    REQUIRE(kv.init_storage_layout());

    for (int i=0;i<200;++i) {
      REQUIRE(kv.put("k"+std::to_string(i), std::string(64, 'a' + (i%26))));
    }
    // при выходе из scope: final_flush_on_close=true — остатки доливаются в SST,
    // purge_wal_files_locked() создаст новый пустой (header-only) сегмент WAL
  }

  auto sst_names = list_sst_sorted(sst_dir.string());
  REQUIRE(!sst_names.empty());

  auto wals = list_wals(wal_dir);
  REQUIRE(wals.size() == 1);
  REQUIRE(fs::file_size(wals[0]) == 4096); // 4 KiB header-only WAL
}
