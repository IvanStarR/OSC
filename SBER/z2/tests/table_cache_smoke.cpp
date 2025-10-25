#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include "cache/table_cache.hpp"
#include "sst/table.hpp"

#include <filesystem>
#include <unistd.h>

using namespace uringkv;

static std::string tmpdir(const char* prefix){
  auto base = std::filesystem::temp_directory_path();
  auto d = base / (std::string(prefix) + std::to_string(::getpid()));
  std::filesystem::create_directories(d);
  return d.string();
}

TEST_CASE("TableCache: hits grow on repeated access") {
  auto dir = tmpdir("uringkv_cache_");

  // создаём SST с данными
  {
    KV kv({.path=dir, .sst_flush_threshold_bytes=1024});
    REQUIRE(kv.init_storage_layout());
    for (int i=0;i<50;++i) {
      REQUIRE(kv.put("k"+std::to_string(i), "v"+std::to_string(i)));
    }
  }

  // найти любой sst (последний)
  std::filesystem::path sst_dir = std::filesystem::path(dir)/"sst";
  std::string sst_path;
  for (auto& e : std::filesystem::directory_iterator(sst_dir)) {
    if (e.is_regular_file() && e.path().extension()==".sst") {
      sst_path = e.path().string();
    }
  }
  REQUIRE_FALSE(sst_path.empty());

  TableCache cache(2);

  auto t1 = cache.get_table(sst_path);
  REQUIRE(t1 != nullptr);
  REQUIRE(t1->good());

  auto before_hits  = cache.hits();
  auto before_miss  = cache.misses();
  auto before_open  = cache.opens();

  auto t2 = cache.get_table(sst_path);
  REQUIRE(t2 != nullptr);
  REQUIRE(t2->good());

  // на второй запрос должен быть hit, без повторного открытия
  REQUIRE(cache.hits() >= before_hits + 1);
  REQUIRE(cache.opens() == before_open);     // не должно расти
  REQUIRE(cache.misses() == before_miss);    // и промахов не добавилось
}
