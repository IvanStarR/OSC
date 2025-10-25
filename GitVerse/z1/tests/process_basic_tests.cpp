#include <catch2/catch_all.hpp>
#include <gitproc/unit.hpp>
#include <gitproc/process.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace gitproc;
namespace fs = std::filesystem;

static fs::path mkd(const char* name){
  auto d = fs::temp_directory_path() / (std::string("gitproc_")+name);
  fs::create_directories(d);
  return d;
}

TEST_CASE("start/stop/status sleep") {
  auto dir = mkd("sleep");
  auto unit = dir / "sleep.unit";
  {
    std::ofstream o(unit);
    o <<
"[Service]\n"
"ExecStart=/bin/sleep 2\n"
"WorkingDirectory=\n"
"TimeoutStopSec=2\n";
  }

  Unit u = Unit::Load(unit);
  fs::current_path(dir); // logs/run в temp каталоге
  auto run_dir  = fs::path("run");
  auto logs_dir = fs::path("logs");

  REQUIRE(ProcessRunner::start(u, run_dir, logs_dir) == 0);

  // Небольшая задержка, чтобы дочерний точно успел перейти в exec
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto st = ProcessRunner::status(u, run_dir);
  REQUIRE(st.running == true);
  REQUIRE(st.pid > 0);

  REQUIRE(ProcessRunner::stop(u, run_dir) == true);
  auto st2 = ProcessRunner::status(u, run_dir);
  REQUIRE(st2.running == false);
}
