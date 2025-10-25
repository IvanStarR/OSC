#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include <filesystem>
#include <unistd.h>

using namespace uringkv;
static std::string md(const char* p){ auto b=std::filesystem::temp_directory_path();
  auto d=b/(std::string(p)+std::to_string(::getpid())); std::filesystem::create_directories(d); return d.string(); }

TEST_CASE("scan merges MemTable and SST") {
  auto dir = md("uringkv_scan_merge_");
  KV kv({.path=dir, .sst_flush_threshold_bytes=1*1024}); // сделать flush
  REQUIRE(kv.init_storage_layout());

  kv.put("a","1"); kv.put("b","2"); kv.put("c","3");
  // заставим flush
  for (int i=0;i<100;i++) kv.put("x"+std::to_string(i), std::string(64,'z'));

  // теперь mem сверху перезапишет нек-рые ключи
  kv.put("b","22"); // overwite в mem
  kv.del("c");      // tombstone в mem

  auto items = kv.scan("a","z");
  // "b" должен быть 22, "c" — отсутствовать
  bool seen_b=false, seen_c=false;
  for (auto& it: items) {
    if (it.key=="b"){ seen_b=true; REQUIRE(it.value=="22"); }
    if (it.key=="c"){ seen_c=true; }
  }
  REQUIRE(seen_b);
  REQUIRE_FALSE(seen_c);
}
