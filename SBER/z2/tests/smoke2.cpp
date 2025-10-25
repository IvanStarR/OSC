// tests/smoke.cpp
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

#include "kv.hpp"
#include "sst/manifest.hpp" // list_sst_sorted

using namespace uringkv;
namespace fs = std::filesystem;

static std::string make_tmp_dir() {
  auto base = fs::temp_directory_path() / fs::path("uringkv_smoke_");
  // unique_path добавит случайный суффикс
  auto dir = base.string() + fs::unique_path().string();
  fs::create_directories(dir);
  return dir;
}

TEST_CASE("uringkv smoke: init, put/get/del/scan, persistence & flush", "[smoke]") {
  const std::string root = make_tmp_dir();
  const std::string wal = (fs::path(root) / "wal").string();
  const std::string sst = (fs::path(root) / "sst").string();

  // Настроим низкий порог flush, чтобы легче триггерить запись SST
  KVOptions opts;
  opts.path                       = root;
  opts.use_uring                 = false; // smoke не зависит от uring
  opts.uring_queue_depth         = 64;
  opts.wal_max_segment_bytes     = 2ull * 1024 * 1024;
  opts.sst_flush_threshold_bytes = 4 * 1024;  // 4KB
  opts.table_cache_capacity      = 8;
  opts.background_compaction     = false;     // детерминированнее
  opts.l0_compact_threshold      = 4;
  opts.flush_mode                = FlushMode::FDATASYNC;

  // 1) init
  {
    KV kv(opts);
    REQUIRE(kv.init_storage_layout());
  }
  REQUIRE(fs::exists(root));
  REQUIRE(fs::exists(wal));
  REQUIRE(fs::exists(sst));

  // 2) put/get
  {
    KV kv(opts);
    REQUIRE(kv.init_storage_layout());

    REQUIRE(kv.put("a", "1"));
    REQUIRE(kv.put("b", "2"));
    REQUIRE(kv.put("c", "3"));

    auto va = kv.get("a");
    auto vb = kv.get("b");
    auto vc = kv.get("c");
    REQUIRE(va.has_value());
    REQUIRE(vb.has_value());
    REQUIRE(vc.has_value());
    REQUIRE(*va == "1");
    REQUIRE(*vb == "2");
    REQUIRE(*vc == "3");

    // scan
    auto items = kv.scan("a", "z");
    REQUIRE(items.size() >= 3);
    // проверим, что "a","b","c" присутствуют и отсортированы
    bool has_a=false, has_b=false, has_c=false;
    std::string prev;
    for (size_t i=0;i<items.size();++i) {
      if (i>0) REQUIRE(items[i-1].key <= items[i].key);
      if (items[i].key == "a" && items[i].value == "1") has_a = true;
      if (items[i].key == "b" && items[i].value == "2") has_b = true;
      if (items[i].key == "c" && items[i].value == "3") has_c = true;
      prev = items[i].key;
    }
    REQUIRE(has_a);
    REQUIRE(has_b);
    REQUIRE(has_c);
  }

  // 3) del + check
  {
    KV kv(opts);
    REQUIRE(kv.init_storage_layout());
    REQUIRE(kv.del("b"));
    auto vb = kv.get("b");
    REQUIRE_FALSE(vb.has_value());
  }

  // 4) persistence after reopen (WAL replay)
  {
    KV kv(opts);
    REQUIRE(kv.init_storage_layout());
    // "a" -> "1", "b" -> tombstone, "c" -> "3"
    auto va = kv.get("a");
    auto vb = kv.get("b");
    auto vc = kv.get("c");
    REQUIRE(va.has_value());
    REQUIRE_FALSE(vb.has_value());
    REQUIRE(vc.has_value());
    REQUIRE(*va == "1");
    REQUIRE(*vc == "3");
  }

  // 5) trigger flush to SST (маленький порог)
  {
    KV kv(opts);
    REQUIRE(kv.init_storage_layout());
    // добавим данных побольше порога
    for (int i=0;i<200;++i) {
      REQUIRE(kv.put("k" + std::to_string(i), std::string(64, 'x'))); // ~13kB всего
    }
    // после разрушения kv финальный flush выполнится (final_flush_on_close=true по умолчанию)
  }

  // Проверим, что в каталоге sst появились файлы *.sst
  {
    auto names = list_sst_sorted(sst);
    // list_sst_sorted возвращает имена файлов, а не полные пути
    REQUIRE(names.size() >= 1);
  }

  // cleanup
  fs::remove_all(root);
}
