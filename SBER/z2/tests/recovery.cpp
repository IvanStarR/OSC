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

static std::string mktemp_dir3(const char* prefix) {
  auto base = std::filesystem::temp_directory_path();
  auto p = base / (std::string(prefix) + std::to_string(::getpid()));
  std::filesystem::create_directories(p);
  return p.string();
}

TEST_CASE("WAL recovery after restart") {
  auto dir = mktemp_dir3("uringkv_recovery_");
  {
    KV kv({.path = dir});
    REQUIRE(kv.init_storage_layout());
    kv.put("x","1");
    kv.put("y","2");
    kv.del("x");
  }
  {
    KV kv({.path = dir});
    REQUIRE_FALSE(kv.get("x").has_value());
    auto vy = kv.get("y");
    REQUIRE(vy.has_value());
    REQUIRE(*vy == "2");
  }
}
