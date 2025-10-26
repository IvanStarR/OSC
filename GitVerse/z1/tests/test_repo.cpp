#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include <sysaudit/repo.hpp>
#include <filesystem>
#include <fstream>
#include <cstdio>

using namespace sysaudit;

static std::filesystem::path make_tmpdir(const std::string& prefix) {
    std::filesystem::path base = std::filesystem::temp_directory_path() / (prefix + "XXXXXX");
    std::string s = base.string();
    std::vector<char> buf(s.begin(), s.end());
    buf.push_back('\0');
    char* p = mkdtemp(buf.data());
    REQUIRE(p != nullptr);
    return std::filesystem::path(p);
}

TEST_CASE("Repo init and default gitignore") {
    auto tmp = make_tmpdir("repo_init_");
    GitRepo repo(tmp);
    REQUIRE(repo.ensure_initialized());
    repo.ensure_default_gitignore();
    std::filesystem::path gi = tmp / ".gitignore";
    REQUIRE(std::filesystem::exists(gi));
}

TEST_CASE("Repo add/modify/remove commit flow") {
    auto tmp = make_tmpdir("repo_flow_");
    GitRepo repo(tmp);
    REQUIRE(repo.ensure_initialized());
    std::ofstream(tmp / "a.txt") << "one";
    REQUIRE(repo.add_and_commit(tmp / "a.txt", "add a.txt"));
    std::ofstream(tmp / "a.txt") << "two";
    REQUIRE(repo.add_and_commit(tmp / "a.txt", "mod a.txt"));
    REQUIRE(repo.remove_and_commit(tmp / "a.txt", "rm a.txt"));
}
