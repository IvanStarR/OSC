// tests/smoke_basic.cpp
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
#include <unistd.h>

using namespace uringkv;
namespace fs = std::filesystem;

// --------- helpers ---------
static std::string mktemp_dir(const char* prefix) {
  auto base = fs::temp_directory_path();
  // pid в имени, чтобы тесты параллельно не пересекались
  for (int i=0;i<1000;++i) {
    auto p = base / (std::string(prefix) + std::to_string(::getpid()) + "_" + std::to_string(i));
    std::error_code ec;
    if (fs::create_directories(p, ec)) return p.string();
  }
  // fallback (почти невозможен)
  auto p = base / (std::string(prefix) + "fallback_" + std::to_string(::getpid()));
  fs::create_directories(p);
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

// --------- tests ---------

TEST_CASE("basic put/get") {
  auto dir = mktemp_dir("uringkv_smoke_");
  KV kv({.path = dir, .background_compaction=false});  // детерминированнее
  REQUIRE(kv.init_storage_layout());

  REQUIRE(kv.put("a","1"));
  REQUIRE(kv.put("b","2"));
  REQUIRE(kv.get("a").value() == "1");
  REQUIRE(kv.get("b").value() == "2");
}

TEST_CASE("overwrite and delete") {
  auto dir = mktemp_dir("uringkv_overdel_");
  KV kv({.path = dir, .background_compaction=false});
  REQUIRE(kv.init_storage_layout());

  REQUIRE(kv.put("k1","v1"));
  REQUIRE(kv.put("k1","v2"));                 // overwrite
  REQUIRE(kv.get("k1").value() == "v2");
  REQUIRE(kv.del("k1"));                      // delete
  REQUIRE_FALSE(kv.get("k1").has_value());
}

TEST_CASE("flush MemTable to SST and recover") {
  auto dir = mktemp_dir("uringkv_flush_");

  {
    KV kv({
      .path=dir,
      .sst_flush_threshold_bytes=1*1024,      // маленький порог -> гарантированный flush
      .background_compaction=false
    });
    REQUIRE(kv.init_storage_layout());

    for (int i=0;i<200;++i) {
      REQUIRE(kv.put("k"+std::to_string(i), std::string(100, 'a' + (i%26))));
    }
    REQUIRE(kv.del("k3"));
    REQUIRE(kv.del("k5"));
  } // деструктор сделает final_flush_on_close (по умолчанию true)

  {
    KV kv({.path=dir, .background_compaction=false});
    // существующие
    REQUIRE(kv.get("k2").has_value());
    // удалённые
    REQUIRE_FALSE(kv.get("k3").has_value());
    REQUIRE_FALSE(kv.get("k5").has_value());
  }
}

TEST_CASE("WAL: purged after MemTable flush") {
  auto dir = mktemp_dir("uringkv_purge_");
  const auto wal_dir = fs::path(dir)/"wal";

  {
    // маленький порог flush -> MemTable уйдёт в SST
    KV kv({
      .path=dir,
      .sst_flush_threshold_bytes=2*1024,
      .background_compaction=false
    });
    REQUIRE(kv.init_storage_layout());
    for (int i=0;i<200;++i)
      REQUIRE(kv.put("k"+std::to_string(i), std::string(64,'a'+(i%26))));
  } // финальный flush + purge WAL

  // после завершения должен остаться один новый WAL-файл с заголовком (4К)
  auto files = list_wals(wal_dir);
  REQUIRE(files.size() == 1);
  REQUIRE(fs::file_size(files[0]) == 4096);
}
