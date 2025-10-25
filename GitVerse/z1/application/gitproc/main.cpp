#include <gitproc/app.hpp>
#include <gitproc/cli.hpp>
#include <spdlog/spdlog.h>

int main(int argc, char** argv) {
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
  return gitproc::App{}.run(argc, argv);
}
