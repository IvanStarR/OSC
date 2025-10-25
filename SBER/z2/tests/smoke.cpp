#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include <filesystem>

using namespace uringkv;

static std::string mktemp_dir(const char* prefix) {
  auto base = std::filesystem::temp_directory_path();
  for (int i=0;i<1000;++i) {
    auto p = base / (std::string(prefix) + std::to_string(::getpid()) + "_" + std::to_string(i));
    if (std::filesystem::create_directories(p)) return p.string();
  }
  return (base / (std::string(prefix) + "fallback")).string();
}

TEST_CASE("basic put/get") {
  auto dir = mktemp_dir("uringkv_smoke_");
  KV kv({.path = dir});
  REQUIRE(kv.init_storage_layout());
  REQUIRE(kv.put("a","1"));
  REQUIRE(kv.put("b","2"));
  REQUIRE(kv.get("a").value() == "1");
  REQUIRE(kv.get("b").value() == "2");
}

TEST_CASE("overwrite and delete") {
  auto dir = mktemp_dir("uringkv_overdel_");
  KV kv({.path = dir});
  REQUIRE(kv.init_storage_layout());
  REQUIRE(kv.put("k1","v1"));
  REQUIRE(kv.put("k1","v2"));                 // overwrite
  REQUIRE(kv.get("k1").value() == "v2");
  REQUIRE(kv.del("k1"));                       // delete
  REQUIRE_FALSE(kv.get("k1").has_value());
}
