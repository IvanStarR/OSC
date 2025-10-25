// tests/wal_purge_header_only.cpp
#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include <filesystem>
#include <unistd.h>

using namespace uringkv;
namespace fs = std::filesystem;

static std::string tmpd2(const char* p){
  auto base = fs::temp_directory_path();
  auto d = base / (std::string(p)+std::to_string(::getpid()));
  fs::create_directories(d);
  return d.string();
}

static std::vector<fs::path> list_wals(const fs::path& wal_dir){
  std::vector<fs::path> out;
  std::error_code ec;
  if (!fs::exists(wal_dir, ec)) return out;
  for (auto& e : fs::directory_iterator(wal_dir, ec)){
    if (ec) break;
    if (e.is_regular_file() && e.path().extension()==".wal") out.push_back(e.path());
  }
  return out;
}

TEST_CASE("purge leaves single 4KiB WAL header segment") {
  auto dir = tmpd2("uringkv_purge_");
  {
    KVOptions o;
    o.path = dir;
    o.use_uring = false;
    o.sst_flush_threshold_bytes = 2*1024; 
    o.final_flush_on_close = true;
    o.background_compaction = false;

    KV kv(o);
    REQUIRE(kv.init_storage_layout());
    for (int i=0;i<200;++i)
      REQUIRE(kv.put("k"+std::to_string(i), std::string(64,'a'+(i%26))));
  }
  auto wdir = fs::path(dir)/"wal";
  auto wals = list_wals(wdir);
  REQUIRE(wals.size() == 1);
  REQUIRE(fs::file_size(wals[0]) == 4096);
}
