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

static std::string mktemp_dir4(const char* prefix) {
  auto base = std::filesystem::temp_directory_path();
  auto p = base / (std::string(prefix) + std::to_string(::getpid()));
  std::filesystem::create_directories(p);
  return p.string();
}

TEST_CASE("works with --uring flag (fallback allowed)") {
  auto dir = mktemp_dir4("uringkv_uring_");
  KV kv({.path = dir, .use_uring = true, .uring_queue_depth = 64});
  REQUIRE(kv.init_storage_layout());
  REQUIRE(kv.put("u1","v1"));
  REQUIRE(kv.get("u1").value() == "v1");
}
