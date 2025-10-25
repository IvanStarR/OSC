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
static std::string mkd(const char* p){ auto b=std::filesystem::temp_directory_path();
  auto d=b/(std::string(p)+std::to_string(::getpid())); std::filesystem::create_directories(d); return d.string(); }

TEST_CASE("flush MemTable to SST and recover") {
  auto dir = mkd("uringkv_flush_");

  {
    KV kv({.path=dir, .sst_flush_threshold_bytes=1*1024});
    REQUIRE(kv.init_storage_layout());
    for (int i=0;i<200;++i) {
      kv.put("k"+std::to_string(i), std::string(100, 'a' + (i%26)));
    }
    kv.del("k3");
    kv.del("k5");
  }

  {
    KV kv({.path=dir});
    REQUIRE(kv.get("k2").has_value());
    REQUIRE_FALSE(kv.get("k3").has_value());
    REQUIRE_FALSE(kv.get("k5").has_value());
  }
}
