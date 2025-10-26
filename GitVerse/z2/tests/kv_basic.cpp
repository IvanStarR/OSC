#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "gitconfig/kv.hpp"
#include <filesystem>
#include <string>
#include <cstdlib>

using namespace gitconfig;

static std::string mkd(const char* tag){
  auto base = std::filesystem::temp_directory_path();
  auto p = base / std::string(tag);
  std::filesystem::remove_all(p);
  std::filesystem::create_directories(p);
  return p.string();
}

TEST_CASE("single node set/get/delete/list") {
  auto repo = mkd("gitconfig_kv_test");
  KVStore kv({repo, "config"});
  std::string err;
  REQUIRE(kv.init(&err));
  REQUIRE(kv.set("/app/db/host", "localhost", &err));
  auto v = kv.get("/app/db/host", &err);
  REQUIRE(v.has_value());
  REQUIRE(*v == "localhost");
  auto list1 = kv.list("/app", false, &err);
  REQUIRE_FALSE(list1.empty());
  REQUIRE(kv.erase("/app/db/host", &err));
  auto v2 = kv.get("/app/db/host", &err);
  REQUIRE_FALSE(v2.has_value());
}
