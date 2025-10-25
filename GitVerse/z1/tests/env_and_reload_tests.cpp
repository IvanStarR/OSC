#include <catch2/catch_all.hpp>
#include <gitproc/app.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace gitproc;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

static fs::path mkd(const char* name){
  auto d = fs::temp_directory_path() / (std::string("gitproc_it4_")+name);
  fs::create_directories(d);
  return d;
}

// утилита запуска App
static int run_app(std::vector<std::string> args){
  std::vector<char*> argv; argv.reserve(args.size()+1);
  for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
  argv.push_back(nullptr);
  return App{}.run((int)args.size(), argv.data());
}

TEST_CASE("EnvironmentFile exports variables to child process") {
  auto repo = mkd("env");
  fs::create_directories(repo / "services");

  auto envf = repo / "svc.env";
  {
    std::ofstream e(envf);
    e << "# comment\nFOO=BAR\nHELLO=world\n";
  }

  auto unit = repo / "services" / "env.unit";
  {
    std::ofstream o(unit);
    o <<
"[Service]\n"
"ExecStart=/bin/sh -c 'echo $FOO-$HELLO; sleep 1'\n"
"EnvironmentFile=" << envf.string() << "\n"
"TimeoutStopSec=1\n";
  }

  fs::current_path(mkd("wd_env"));
  REQUIRE(run_app({"gitproc","start","--repo",repo.string(),"--unit","services/env.unit"}) == 0);

  // подождём, чтобы процесс успел записать в лог
  std::this_thread::sleep_for(500ms);

  // читаем stdout
  auto outp = fs::path("logs") / "env.out";
  REQUIRE(fs::exists(outp));

  std::ifstream in(outp);
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  REQUIRE(content.find("BAR-world") != std::string::npos);

  REQUIRE(run_app({"gitproc","stop","--repo",repo.string(),"--unit","services/env.unit"}) == 0);
}

TEST_CASE("ExecReload keeps PID and executes command") {
  auto repo = mkd("reload");
  fs::create_directories(repo / "services");

  auto mark = repo / "reload.mark";

  auto unit = repo / "services" / "svc.unit";
  {
    std::ofstream o(unit);
    o <<
"[Service]\n"
"ExecStart=/bin/sleep 5\n"
"ExecReload=/bin/sh -c 'echo reloaded > " << mark.string() << "'\n"
"TimeoutStopSec=1\n";
  }

  fs::current_path(mkd("wd_reload"));
  REQUIRE(run_app({"gitproc","start","--repo",repo.string(),"--unit","services/svc.unit"}) == 0);

  // pid до
  std::string before;
  {
    FILE* f = popen("cat run/svc.pid 2>/dev/null", "r");
    if (f){ char buf[64]={0}; size_t n=fread(buf,1,sizeof(buf)-1,f); before.assign(buf,buf+n); pclose(f); }
  }
  REQUIRE(!before.empty());

  // reload
  REQUIRE(run_app({"gitproc","reload","--repo",repo.string(),"--unit","services/svc.unit"}) == 0);

  // pid после — тот же
  std::string after;
  {
    FILE* f = popen("cat run/svc.pid 2>/dev/null", "r");
    if (f){ char buf[64]={0}; size_t n=fread(buf,1,sizeof(buf)-1,f); after.assign(buf,buf+n); pclose(f); }
  }
  REQUIRE(before == after);

  // mark-файл создан
  REQUIRE(fs::exists(mark));

  REQUIRE(run_app({"gitproc","stop","--repo",repo.string(),"--unit","services/svc.unit"}) == 0);
}
