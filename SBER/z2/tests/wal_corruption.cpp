#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include "util.hpp"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdio>

using namespace uringkv;

static std::string mktemp_dir5(const char* prefix) {
  auto base = std::filesystem::temp_directory_path();
  auto p = base / (std::string(prefix) + std::to_string(::getpid()));
  std::filesystem::create_directories(p);
  return p.string();
}

TEST_CASE("WAL tail corruption: first record survives") {
  auto dir = mktemp_dir5("uringkv_corrupt_");
  {
    KV kv({.path = dir});
    REQUIRE(kv.init_storage_layout());
    kv.put("a","111");
    kv.put("b","222");
  }

  auto wal_path = std::filesystem::path(dir) / "wal" / "000001.wal";
  REQUIRE(std::filesystem::exists(wal_path));
  {
    std::fstream f(wal_path, std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(f.good());
    f.seekg(0, std::ios::end);
    auto len = f.tellg();
    REQUIRE(len > 0);
    f.seekp(len - 1);
    char bad = 0xFF;
    f.write(&bad, 1);
    f.flush();
  }

  {
    KV kv({.path = dir});
    auto va = kv.get("a");
    REQUIRE(va.has_value());
    REQUIRE(*va == "111");

    auto vb = kv.get("b");
    (void)vb;
  }
}
