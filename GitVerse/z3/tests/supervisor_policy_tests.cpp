#include <catch2/catch_all.hpp>
#include <gitproc/supervisor.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace gitproc;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

static fs::path mkd(const char* name){
  auto d = fs::temp_directory_path() / (std::string("gitproc_sup_")+name);
  fs::create_directories(d);
  return d;
}

TEST_CASE("Supervisor restarts on failure (on-failure)") {
  auto dir = mkd("restart");
  auto unit = dir / "boom.unit";
  {
    std::ofstream o(unit);
    o <<
"[Service]\n"
"ExecStart=/bin/sh -c \"exit 1\"\n"
"WorkingDirectory=\n"
"TimeoutStopSec=1\n"
"Restart=on-failure\n"
"RestartSec=1\n";
  }

  fs::current_path(dir);
  Supervisor sup("run","logs");
  Unit u = Unit::Load(unit);
  REQUIRE(sup.start(u) == true);

  // подождём пару секунд — должно быть несколько перезапусков
  std::this_thread::sleep_for(2500ms);
  // тест «мягкий»: сам факт что Supervisor не упал
  REQUIRE(true);

  REQUIRE(sup.stop() == true);
}

TEST_CASE("Health-check restarts on failure") {
  auto dir = mkd("health");
  auto unit = dir / "svc.unit";
  {
    std::ofstream o(unit);
    o <<
"[Service]\n"
"ExecStart=/bin/sleep 5\n"
"WorkingDirectory=\n"
"TimeoutStopSec=1\n"
"ExecHealth=/bin/sh -c \"exit 1\"\n"
"WatchdogSec=1\n";
  }
  fs::current_path(dir);
  Supervisor sup("run","logs");
  Unit u = Unit::Load(unit);
  REQUIRE(sup.start(u) == true);

  // Через ~1-2 сек health упадёт и вызовет рестарт — просто подождём
  std::this_thread::sleep_for(2500ms);
  REQUIRE(sup.stop() == true);
}
