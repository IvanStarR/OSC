// tests/no_deadlock_on_close.cpp
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

static std::string tdir3(const char* p){
  auto base = fs::temp_directory_path();
  auto d = base / (std::string(p)+std::to_string(::getpid()));
  fs::create_directories(d);
  return d.string();
}

TEST_CASE("no deadlock on destructor with final flush, no background compaction") {
  auto dir = tdir3("uringkv_nolock_");
  {
    KVOptions o;
    o.path = dir;
    o.use_uring = false;
    o.final_flush_on_close = true;
    o.background_compaction = false; // важно!
    o.sst_flush_threshold_bytes = 4*1024; // спровоцировать несколько SST
    KV kv(o);
    REQUIRE(kv.init_storage_layout());
    for (int i=0;i<5000;++i)
      REQUIRE(kv.put("k"+std::to_string(i), "v"+std::to_string(i)));
  } // если бы был дедлок — тест бы завис/падал по timeout в CI
  SUCCEED();
}
