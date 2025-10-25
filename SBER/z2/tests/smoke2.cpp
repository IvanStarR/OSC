// tests/smoke.cpp
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>
#include <system_error>
#include <cstring>
#include <array>

#include <unistd.h>   // mkdtemp
#include <stdlib.h>   // mkdtemp

#include "kv.hpp"
#include "sst/manifest.hpp" // list_sst_sorted

using namespace uringkv;
namespace fs = std::filesystem;

// Создание временного каталога (Linux): mkdtemp безопасно подставит уникальный суффикс
static std::string make_tmp_dir() {
  std::string tmpl = (fs::temp_directory_path() / "uringkv_smoke_XXXXXX").string();

  // mkdtemp требует char* с изменяемой памятью и нулём в конце
  std::vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back('\0');

  char* res = ::mkdtemp(buf.data());
  REQUIRE(res != nullptr); // если не удалось — тест падает с сообщением от Catch2

  return std::string(res);
}

TEST_CASE("uringkv smoke: init, put/get/del/scan, persistence & flush", "[smoke]") {
  const std::string root = make_tmp_dir();
  const std::string wal = (fs::path(root) / "wal").string();
  const std::string sst = (fs::path(root) / "sst").string();

  // Низкий порог flush, чтобы легко получить SST
  KVOptions opts;
  opts.path                       = root;
  opts.use_uring                 = false;
  opts.uring_queue_depth         = 64;
  opts.wal_max_segment_bytes     = 2ull * 1024 * 1024;
  opts.sst_flush_threshold_bytes = 4 * 1024;  // 4KB
  opts.table_cache_capacity      = 8;
  opts.background_compaction     = false;     // без фоновых потоков — детерминированно
  opts.l0_compact_threshold      = 4;
  opts.flush_mode                = FlushMode::FDATASYNC;
  opts.final_flush_on_close      = false;     // <-- ВАЖНО: чтобы деструктор не делал финальный flush

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
    bool has_a=false, has_b=false, has_c=false;
    for (size_t i=0;i<items.size();++i) {
      if (i>0) REQUIRE(items[i-1].key <= items[i].key); // отсортировано
      if (items[i].key == "a" && items[i].value == "1") has_a = true;
      if (items[i].key == "b" && items[i].value == "2") has_b = true;
      if (items[i].key == "c" && items[i].value == "3") has_c = true;
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
    for (int i=0;i<200;++i) {
      REQUIRE(kv.put("k" + std::to_string(i), std::string(64, 'x'))); // ~13kB всего
    }
    // при разрушении kv произойдёт финальный flush (final_flush_on_close = true)
  }

  // Проверим, что появились файлы *.sst
  {
    auto names = list_sst_sorted(sst);
    REQUIRE(names.size() >= 1);
  }

  // cleanup
  std::error_code ec;
  fs::remove_all(root, ec); // на CI не валим тест, если не получилось удалить
}
