#include <catch2/catch_all.hpp>
#include <gitproc/app.hpp>
#include <filesystem>
#include <fstream>

using namespace gitproc;
namespace fs = std::filesystem;

static fs::path mkd(const char* name){
  auto d = fs::temp_directory_path() / (std::string("gitproc_cli_")+name);
  fs::create_directories(d);
  return d;
}

static int run_app(std::vector<std::string> args){
  std::vector<char*> argv; argv.reserve(args.size()+1);
  for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
  argv.push_back(nullptr);
  return App{}.run((int)args.size(), argv.data());
}

TEST_CASE("start/status/stop with --repo/--unit") {
  auto repo = mkd("repo");
  fs::create_directories(repo / "services");
  auto unit = repo / "services" / "sleep.unit";
  {
    std::ofstream o(unit);
    o << "[Service]\nExecStart=/bin/sleep 2\nWorkingDirectory=\nTimeoutStopSec=1\n";
  }

  fs::current_path(mkd("wd")); // чтобы не зависеть от CWD

  REQUIRE(run_app({"gitproc","start","--repo",repo.string(),"--unit","services/sleep.unit"}) == 0);

  // статус
  REQUIRE(run_app({"gitproc","status","--repo",repo.string(),"--unit","services/sleep.unit"}) == 0);

  // стоп
  REQUIRE(run_app({"gitproc","stop","--repo",repo.string(),"--unit","services/sleep.unit"}) == 0);
}
