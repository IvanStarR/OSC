#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "gitconfig/kv.hpp"
#include "gitconfig/repo.hpp"
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

TEST_CASE("sync_basic", "[sync_basic]") {
  auto work1 = mkd("gitconfig_sync_w1");
  auto bare  = mkd("gitconfig_sync_bare");
  auto work2 = mkd("gitconfig_sync_w2");

  auto r = run_sh("git -C " + shell_quote(bare) + " init --bare 2>&1");
  REQUIRE(r.code == 0);

  KVStore kv1({work1, "config"});
  std::string err;
  REQUIRE(kv1.init(&err));
  REQUIRE(kv1.set("/app/a", "1", &err));

  Repo repo1(work1);
  REQUIRE(repo1.set_remote("origin", bare, &err));
  REQUIRE(repo1.push("origin", "main", &err));

  auto r2 = run_sh("git clone " + shell_quote(bare) + " " + shell_quote(work2) + " 2>&1");
  REQUIRE(r2.code == 0);

  KVStore kv2({work2, "config"});
  auto v = kv2.get("/app/a", &err);
  REQUIRE(v.has_value());
  REQUIRE(*v == "1");

  REQUIRE(kv2.set("/app/b", "2", &err));
  Repo repo2(work2);
  REQUIRE(repo2.push("origin", "main", &err));

  REQUIRE(repo1.pull("origin", "main", &err));
  auto v1b = kv1.get("/app/b", &err);
  REQUIRE(v1b.has_value());
  REQUIRE(*v1b == "2");
}
