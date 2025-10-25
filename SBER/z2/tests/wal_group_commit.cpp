// tests/wal_group_commit.cpp
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
namespace fs = std::filesystem;

static std::string tmpd(const char* p){
  auto base = fs::temp_directory_path();
  auto d = base / (std::string(p)+std::to_string(::getpid()));
  fs::create_directories(d);
  return d.string();
}

TEST_CASE("WAL group-commit threshold works (replay stays correct)") {
  auto dir = tmpd("uringkv_gc_");
  {
    KVOptions o;
    o.path = dir;
    o.use_uring = false;
    o.wal_group_commit_bytes = 1;         
    o.final_flush_on_close = false;    
    o.sst_flush_threshold_bytes = (1ull<<30);
    o.background_compaction = false;

    KV kv(o);
    REQUIRE(kv.init_storage_layout());
    for (int i=0;i<10;++i) REQUIRE(kv.put("k"+std::to_string(i), "v"));
    REQUIRE(kv.del("k3"));
    REQUIRE(kv.del("k7"));
  }
  {
    KV kv({.path=dir, .use_uring=false, .background_compaction=false});
    for (int i=0;i<10;++i){
      auto v = kv.get("k"+std::to_string(i));
      if (i==3 || i==7) REQUIRE_FALSE(v.has_value());
      else { REQUIRE(v.has_value()); REQUIRE(*v=="v"); }
    }
  }
}
