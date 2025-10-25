#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include <filesystem>
#include <set>

using namespace uringkv;

static std::string mktemp_dir2(const char* prefix) {
  auto base = std::filesystem::temp_directory_path();
  auto p = base / (std::string(prefix) + std::to_string(::getpid()));
  std::filesystem::create_directories(p);
  return p.string();
}

TEST_CASE("scan range lexicographic") {
  auto dir = mktemp_dir2("uringkv_scan_");
  KV kv({.path = dir});
  REQUIRE(kv.init_storage_layout());
  kv.put("a","1"); kv.put("b","2"); kv.put("c","3"); kv.put("aa","11");
  // Диапазон [a, b] (в текущей реализации сравнение по <= end)
  auto items = kv.scan("a","b");
  REQUIRE(!items.empty());
  // проверим монотонность ключей
  for (size_t i=1;i<items.size();++i) {
    REQUIRE(items[i-1].key <= items[i].key);
  }
  for (auto& it: items) {
    REQUIRE(it.key >= "a");
    REQUIRE(it.key <= "b");
  }
}
