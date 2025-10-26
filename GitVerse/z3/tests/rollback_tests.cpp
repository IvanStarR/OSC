#include <catch2/catch_all.hpp>
#include <gitproc/supervisor.hpp>
#include <filesystem>
#include <fstream>
#include <cstdlib>

using namespace gitproc;
namespace fs = std::filesystem;

static fs::path mkd(const char* name){
  auto d = fs::temp_directory_path() / (std::string("gitproc_rb_")+name);
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}

static void sh(const std::string& cmd, const fs::path& wd){
  auto full = "cd \"" + wd.string() + "\" && " + cmd + " >/dev/null 2>&1";
  REQUIRE(std::system(full.c_str()) == 0);
}

TEST_CASE("Rollback unit to specific commit") {
  auto repo = mkd("repo");
  fs::create_directories(repo/"services");

  sh("git init", repo);
  sh("git config user.email test@example.com", repo);
  sh("git config user.name tester", repo);

  auto unit = repo/"services"/"svc.unit";

  {
    std::ofstream(unit) << "[Service]\nExecStart=/bin/sleep 1\n";
    sh("git add .", repo);
    sh("git commit -m v1", repo);
  }
  FILE* p = popen(("cd \"" + repo.string() + "\" && git rev-parse HEAD").c_str(), "r");
  REQUIRE(p);
  char buf[128] = {0};
  REQUIRE(fread(buf,1,sizeof(buf)-1,p) > 0);
  pclose(p);
  std::string c1(buf); while(!c1.empty() && (c1.back()=='\n'||c1.back()=='\r')) c1.pop_back();

  {
    std::ofstream(unit) << "[Service]\nExecStart=/bin/sleep 2\n";
    sh("git add .", repo);
    sh("git commit -m v2", repo);
  }

  Supervisor sv(fs::current_path());
  REQUIRE(sv.open_repo(repo.string(), "main"));

  REQUIRE(sv.rollback_unit("svc", c1));

  std::ifstream in(unit);
  std::string s((std::istreambuf_iterator<char>(in)), {});
  REQUIRE(s.find("sleep 1") != std::string::npos);

  REQUIRE(sv.stop());
}
