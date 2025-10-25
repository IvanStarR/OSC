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

using namespace uringkv;
namespace fs = std::filesystem;

// ----------------- helpers (простые и детерминированные) -----------------
static std::string mktemp_dir(const char* prefix) {
  auto base = fs::temp_directory_path();
  // pid в имени — тесты не пересекаются при параллельном запуске
  for (int i=0;i<1000;++i) {
    auto p = base / (std::string(prefix) + std::to_string(::getpid()) + "_" + std::to_string(i));
    std::error_code ec;
    if (fs::create_directories(p, ec)) return p.string();
  }
  // fallback (маловероятно)
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

// ========================= ITERATION 1 TESTS ==============================

// 1) Storage layout инициализируется (path/wal/sst созданы)
TEST_CASE("iter1: init storage layout") {
  const auto dir = mktemp_dir("uringkv_init_");
  KV kv({
    .path = dir,
    .background_compaction = false // итерация 1 — без фона
  });
  REQUIRE(kv.init_storage_layout());

  REQUIRE(fs::exists(dir));
  REQUIRE(fs::exists(fs::path(dir)/"wal"));
  REQUIRE(fs::exists(fs::path(dir)/"sst"));
}

// 2) Базовые операции put/get/del работают в пределах одного процесса
TEST_CASE("iter1: basic put/get/del") {
  const auto dir = mktemp_dir("uringkv_basic_");
  KV kv({
    .path = dir,
    .background_compaction = false
  });
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

// 3) WAL replay: данные видны после пересоздания инстанса (без flush в SST)
TEST_CASE("iter1: WAL replay persistence") {
  const auto dir = mktemp_dir("uringkv_replay_");

  {
    KV kv({
      .path = dir,
      .background_compaction = false
    });
    REQUIRE(kv.init_storage_layout());
    REQUIRE(kv.put("x","1"));
    REQUIRE(kv.put("y","2"));
    REQUIRE(kv.del("x"));
  } // dtor: final_flush_on_close=true, но MemTable может не превысить порог — остаётся в WAL

  {
    KV kv({
      .path = dir,
      .background_compaction = false
    });
    // x был удалён, y должен сохраниться
    REQUIRE_FALSE(kv.get("x").has_value());
    auto vy = kv.get("y");
    REQUIRE(vy.has_value());
    REQUIRE(*vy == std::string("2"));
  }
}

// 4) Принудительный flush MemTable в SST + purge WAL
TEST_CASE("iter1: MemTable flush creates SST and purges WAL") {
  const auto dir = mktemp_dir("uringkv_flush_");
  const auto wal_dir = fs::path(dir) / "wal";
  const auto sst_dir = fs::path(dir) / "sst";

  {
    KV kv({
      .path = dir,
      .sst_flush_threshold_bytes = 2*1024,   // низкий порог → гарантированный flush
      .background_compaction = false
    });
    REQUIRE(kv.init_storage_layout());

    // ~12.8 KiB — точно превысим порог
    for (int i=0;i<200;++i) {
      REQUIRE(kv.put("k"+std::to_string(i), std::string(64, 'a' + (i%26))));
    }
    // dtor: final_flush_on_close=true → остатки дольются в SST и WAL будет «очищен»
  }

  // Проверяем: есть хотя бы один *.sst
  auto sst_names = list_sst_sorted(sst_dir.string());
  REQUIRE(!sst_names.empty());

  // И ровно один новый WAL-файл размером 4 Киб (заголовок сегмента)
  auto wals = list_wals(wal_dir);
  REQUIRE(wals.size() == 1);
  REQUIRE(fs::file_size(wals[0]) == 4096);
}
