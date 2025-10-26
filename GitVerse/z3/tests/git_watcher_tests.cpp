#include <catch2/catch_all.hpp>
#include <gitproc/git.hpp>
#include <gitproc/watcher.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>

using namespace gitproc;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

static fs::path mkd(const char* name){
  auto d = fs::temp_directory_path() / (std::string("gitproc_repo_")+name);
  fs::create_directories(d);
  return d;
}

TEST_CASE("GitRepo resolve and revision") {
  auto repo_root = mkd("resolve");
  fs::create_directories(repo_root / "services");

  auto unit = repo_root / "services" / "web.unit";
  {
    std::ofstream o(unit);
    o << "[Service]\nExecStart=/bin/sleep 1\nWorkingDirectory=\nTimeoutStopSec=1\n";
  }

  auto repo = GitRepo::open_local(repo_root);
  auto p1 = repo.resolve_unit("web");
  auto p2 = repo.resolve_unit("services/web.unit");
  auto p3 = repo.resolve_unit(unit.string());

  REQUIRE(p1 == unit);
  REQUIRE(fs::equivalent(p2, unit));
  REQUIRE(fs::equivalent(p3, unit));

  auto rev1 = repo.unit_revision(unit);
  REQUIRE(rev1.has_value());
}

TEST_CASE("Watcher detects change via mtime/size (with priming)") {
  auto repo_root = mkd("watch");
  fs::create_directories(repo_root / "services");

  auto unit = repo_root / "services" / "job.unit";
  {
    std::ofstream o(unit);
    o << "[Service]\nExecStart=/bin/sleep 1\nWorkingDirectory=\nTimeoutStopSec=1\n";
  }

  auto repo = GitRepo::open_local(repo_root);

  int changes = 0;
  Watcher w(repo, "services/job.unit", "main",
            [&](const fs::path& p){ (void)p; changes++; });

  REQUIRE(w.poll_once() == false);
  REQUIRE(changes == 0);

  REQUIRE(w.poll_once() == false);
  REQUIRE(changes == 0);

  {
    std::ofstream o(unit, std::ios::app);
    o << "# touch\n";
    o.flush();
  }

  bool triggered = false;
  for (int i = 0; i < 20; ++i) { // до ~2 секунд
    if (w.poll_once()) { triggered = true; break; }
    std::this_thread::sleep_for(100ms);
  }
  REQUIRE(triggered == true);
  REQUIRE(changes >= 1);
}
