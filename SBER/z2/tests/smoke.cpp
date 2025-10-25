#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif
#include "kv.hpp"
#include <filesystem>

TEST_CASE("basic put/get") {
  using namespace uringkv;
  auto tmp = std::filesystem::temp_directory_path() / "uringkv_test";
  KV kv({.path = tmp.string()});
  kv.init_storage_layout();
  REQUIRE(kv.put("a","1"));
  REQUIRE(kv.get("a").value() == "1");
}
