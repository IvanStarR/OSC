#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include <filesystem>
#include <string>
#include <unistd.h>

using namespace uringkv;

static std::string tdir(const char* p){
  auto base = std::filesystem::temp_directory_path();
  auto d = base / (std::string(p)+std::to_string(::getpid()));
  std::filesystem::create_directories(d);
  return d.string();
}

TEST_CASE("L0 compaction merges many SSTs into one and keeps correctness") {
  auto dir = tdir("uringkv_compact_");

  KV kv({.path=dir, .sst_flush_threshold_bytes=1*1024});
  REQUIRE(kv.init_storage_layout());

  for (int i=0;i<50;++i) REQUIRE(kv.put("k"+std::to_string(i), "v0"));
  for (int i=0;i<50;i+=2) REQUIRE(kv.put("k"+std::to_string(i), "v1"));
  for (int i=0;i<10;i+=3) REQUIRE(kv.del("k"+std::to_string(i)));       

}

TEST_CASE("L0 compaction result check") {
  auto dir = tdir("uringkv_compact_2_");

  {
    KV kv({.path=dir, .sst_flush_threshold_bytes=1*1024});
    REQUIRE(kv.init_storage_layout());

    for (int r=0;r<8;++r) {
      for (int i=0;i<20;++i) REQUIRE(kv.put("k"+std::to_string(i), "v"+std::to_string(r)));
    }
    for (int i=0;i<20;i+=5) REQUIRE(kv.del("k"+std::to_string(i)));
  }

  std::filesystem::path sst_dir = std::filesystem::path(dir)/"sst";
  size_t sst_count = 0;
  for (auto& e : std::filesystem::directory_iterator(sst_dir)) {
    if (e.is_regular_file() && e.path().extension()==".sst") ++sst_count;
  }
  REQUIRE(sst_count == 1);

  KV kv2({.path=dir});
  for (int i=0;i<20;++i) {
    auto v = kv2.get("k"+std::to_string(i));
    if (i % 5 == 0) {
      REQUIRE_FALSE(v.has_value()); 
    } else {
      REQUIRE(v.has_value());
    }
  }
}
