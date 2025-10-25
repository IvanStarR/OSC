#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "sst/footer.hpp"

using namespace uringkv;
namespace fs = std::filesystem;

static std::string mktmp3(const char* p){
  auto b=fs::temp_directory_path();
  auto d=b/(std::string(p)+std::to_string(::getpid()));
  fs::create_directories(d);
  return d.string();
}

TEST_CASE("SST has footer and sparse index; get finds keys near boundaries") {
  auto dir = mktmp3("uringkv_sstidx_");

  {
    KV kv({.path=dir, .sst_flush_threshold_bytes=64*1024});
    REQUIRE(kv
