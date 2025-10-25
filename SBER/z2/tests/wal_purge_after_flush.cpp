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
#include <vector>
#include <string>

using namespace uringkv;

static std::string mkd(const char* p){
  auto b = std::filesystem::temp_directory_path();
  auto d = b / (std::string(p)+std::to_string(::getpid()));
  std::filesystem::create_directories(d);
  return d.string();
}

static std::vector<std::filesystem::path> list_wals(const std::filesystem::path& wal_dir){
  std::vector<std::filesystem::path> out;
  if (!std::filesystem::exists(wal_dir)) return out;
  for (auto& e : std::filesystem::directory_iterator(wal_dir)){
    if (e.is_regular_file() && e.path().extension()==".wal") out.push_back(e.path());
  }
  return out;
}

TEST_CASE("WAL: purged after MemTable flush") {
  auto dir = mkd("uringkv_purge_");
  const auto wal_dir = std::filesystem::path(dir)/"wal";

  {
    KV kv({.path=dir, .sst_flush_threshold_bytes=2*1024});
    REQUIRE(kv.init_storage_layout());
    for (int i=0;i<200;++i)
      kv.put("k"+std::to_string(i), std::string(64,'a'+(i%26)));
  }

  auto files = list_wals(wal_dir);
  REQUIRE(files.size() == 1);
  REQUIRE(std::filesystem::file_size(files[0]) == 4096);
}
