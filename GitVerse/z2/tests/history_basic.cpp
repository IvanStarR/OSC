#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "gitconfig/kv.hpp"
#include "gitconfig/history.hpp"
#include <filesystem>
#include <string>

using namespace gitconfig;

static std::string mkd(const char* tag){
  auto base = std::filesystem::temp_directory_path();
  auto p = base / std::string(tag);
  std::filesystem::remove_all(p);
  std::filesystem::create_directories(p);
  return p.string();
}

TEST_CASE("history get by commit", "[history]") {
  auto repo = mkd("gitconfig_hist_repo");
  KVStore kv({repo, "config"});
  std::string err;
  REQUIRE(kv.init(&err));
  REQUIRE(kv.set("/k/a", "v1", &err));
  REQUIRE(kv.set("/k/a", "v2", &err));
  auto hist = History::list_for_key(kv, "/k/a", 10, &err);
  REQUIRE(hist.size() >= 2);
  auto sha_old = hist.back().sha;
  auto v_old = kv.get_at("/k/a", sha_old, &err);
  REQUIRE(v_old.has_value());
  REQUIRE(*v_old == "v1");
}
