#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include <sysaudit/filter.hpp>
#include <filesystem>

using namespace sysaudit;

TEST_CASE("Basic suffix filter") {
    PathFilter f("/root", {".tmp", ".swp", ".log", "~"});
    REQUIRE(f.is_ignored("/root/a/b/file.tmp", false));
    REQUIRE(f.is_ignored("/root/a/b/file.swp", false));
    REQUIRE(f.is_ignored("/root/a/b/file.log", false));
    REQUIRE(f.is_ignored("/root/a/b/file~", false));
    REQUIRE_FALSE(f.is_ignored("/root/a/b/file.bin", false));
}

TEST_CASE("Dotgit is always ignored") {
    PathFilter f("/root", {});
    REQUIRE(f.is_ignored("/root/.git/index", false));
    REQUIRE(f.is_ignored("/root/x/.git/obj", true));
}

TEST_CASE("Pattern include/exclude") {
    PathFilter f("/root", {});
    f.add_pattern("*.bak", false);
    f.add_pattern("!/keep/*.bak", true);
    REQUIRE(f.is_ignored("/root/dir/a.bak", false));
    REQUIRE_FALSE(f.is_ignored("/root/keep/a.bak", false));
}

TEST_CASE("Anchored and dir-only") {
    PathFilter f("/root", {});
    f.add_pattern("/logs/", false);
    f.add_pattern("!*_keep/", true);
    REQUIRE(f.is_ignored("/root/logs", true));
    REQUIRE(f.is_ignored("/root/logs/app/x.txt", false));
    REQUIRE_FALSE(f.is_ignored("/root/logs_keep/app/x.txt", false));
}
