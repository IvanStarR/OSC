#include <catch2/catch_all.hpp>
#include <gitproc/io.hpp>
#include <filesystem>
#include <fstream>

using namespace gitproc;
namespace fs = std::filesystem;

static fs::path mkd(const char* name){
  auto d = fs::temp_directory_path() / (std::string("gitproc_logrot_")+name);
  fs::create_directories(d);
  return d;
}

TEST_CASE("log rotation rolls files") {
  auto d = mkd("rot");
  auto base = d / "test.log";

  {
    std::ofstream o(base);
    for (int i=0;i<2000;i++) o << "x";
  }
  io::rotate_logs(base, 1024, 3);
  REQUIRE(fs::exists(base));
  REQUIRE(fs::exists(d / "test.log.1"));

  {
    std::ofstream o(base, std::ios::app);
    for (int i=0;i<2000;i++) o << "y";
  }
  io::rotate_logs(base, 1024, 3);
  REQUIRE(fs::exists(d / "test.log.1"));
}
