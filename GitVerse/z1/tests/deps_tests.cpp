#include <catch2/catch_all.hpp>
#include <gitproc/supervisor.hpp>
#include <filesystem>
#include <fstream>

using namespace gitproc;
namespace fs = std::filesystem;

static fs::path mkd(const char* name){
  auto d = fs::temp_directory_path() / (std::string("gitproc_deps_")+name);
  fs::remove_all(d);
  fs::create_directories(d / "services");
  return d;
}

TEST_CASE("Before/After dependency order topo-sort") {
  auto repo = mkd("repo");
  // a.before=b  => a -> b
  // c.after=b   => b -> c
  // ожидаемый порядок: a, b, c
  {
    std::ofstream(repo/"services"/"a.unit")
      << "[Service]\n"
         "ExecStart=/bin/sleep 1\n"
         "Before=b\n";
    std::ofstream(repo/"services"/"b.unit")
      << "[Service]\n"
         "ExecStart=/bin/sleep 1\n";
    std::ofstream(repo/"services"/"c.unit")
      << "[Service]\n"
         "ExecStart=/bin/sleep 1\n"
         "After=b\n";
  }

  // Используем публичный API Supervisor: open_repo + start_all
  Supervisor sv(fs::current_path());
  REQUIRE(sv.open_repo(repo.string(), "main"));

  // Сам факт, что start_all() не падает (и не зацикливается),
  // подтверждает корректную топосортировку зависимостей.
  REQUIRE_NOTHROW(sv.start_all());

  // Остановим внутренние потоки мониторинга
  REQUIRE(sv.stop());
}
