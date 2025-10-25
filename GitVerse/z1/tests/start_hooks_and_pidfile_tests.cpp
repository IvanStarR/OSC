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
  auto d = fs::temp_directory_path() / (std::string("gitproc_it6_")+name);
  fs::create_directories(d);
  return d;
}

static int run_app(std::vector<std::string> args){
  std::vector<char*> argv; argv.reserve(args.size()+1);
  for (auto& s : args) argv.push_back(const_cast<char*>(s.c_str()));
  argv.push_back(nullptr);
  return App{}.run((int)args.size(), argv.data());
}

TEST_CASE("ExecStartPre and ExecStartPost are executed") {
  auto repo = mkd("hooks");
  fs::create_directories(repo / "services");

  auto pre = repo / "pre.mark";
  auto post = repo / "post.mark";

  auto unit = repo / "services" / "svc.unit";
  {
    std::ofstream o(unit);
    o <<
"[Service]\n"
"WorkingDirectory=" << repo.string() << "\n"
"ExecStartPre=/bin/sh -c 'echo pre > " << pre.string() << "'\n"
"ExecStart=/bin/sleep 1\n"
"ExecStartPost=/bin/sh -c 'echo post > " << post.string() << "'\n"
"TimeoutStopSec=1\n";
  }

  fs::current_path(mkd("wd_hooks"));
  REQUIRE(run_app({"gitproc","start","--repo",repo.string(),"--unit","services/svc.unit"}) == 0);

  std::this_thread::sleep_for(400ms); // время на post
  REQUIRE(fs::exists(pre));
  REQUIRE(fs::exists(post));

  REQUIRE(run_app({"gitproc","stop","--repo",repo.string(),"--unit","services/svc.unit"}) == 0);
}

TEST_CASE("PIDFile is honored and overrides fork pid") {
  auto repo = mkd("pidfile");
  fs::create_directories(repo / "services");

  auto pidf = repo / "svc.pid";

  // Процесс: шелл, который записывает $$ (PID шела), затем спит.
  // Шелл не форкается дальше — но мы тестируем сам механизм ожидания PIDFile.
  auto unit = repo / "services" / "svc.unit";
  {
    std::ofstream o(unit);
    o <<
"[Service]\n"
"WorkingDirectory=" << repo.string() << "\n"
"ExecStart=/bin/sh -c 'echo $$ > " << pidf.string() << "; sleep 2'\n"
"PIDFile=" << pidf.string() << "\n"
"PIDFileTimeoutSec=2\n"
"TimeoutStopSec=1\n";
  }

  fs::current_path(mkd("wd_pidfile"));
  REQUIRE(run_app({"gitproc","start","--repo",repo.string(),"--unit","services/svc.unit"}) == 0);

  // читаем pid из run/svc.pid
  std::this_thread::sleep_for(300ms);
  std::ifstream in("run/svc.pid"); REQUIRE(in.good());
  int p_run=-1; in >> p_run; REQUIRE(p_run > 0);

  // должен совпадать с содержимым собственного pid-файла
  std::ifstream in2(pidf); int p2=-1; in2 >> p2; REQUIRE(p2 == p_run);

  REQUIRE(run_app({"gitproc","stop","--repo",repo.string(),"--unit","services/svc.unit"}) == 0);
}
