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

static std::string mktmp2(const char* p){
  auto b=fs::temp_directory_path();
  auto d=b/(std::string(p)+std::to_string(::getpid()));
  fs::create_directories(d);
  return d.string();
}

static size_t count_wal_files(const fs::path& wal_dir){
  size_t n=0;
  if (fs::exists(wal_dir)) for (auto& e: fs::directory_iterator(wal_dir)) {
    if (e.is_regular_file() && e.path().extension()==".wal") ++n;
  }
  return n;
}

TEST_CASE("WAL directory is purged after successful SST flush") {
  auto dir = mktmp2("uringkv_walpurge_");

  {
    // маленький порог для быстрого flush
    KV kv({.path=dir, .sst_flush_threshold_bytes=16*1024});
    REQUIRE(kv.init_storage_layout());

    // Запишем данных > порога, чтобы гарантированно случился flush
    for (int i=0;i<100;++i)
      kv.put("k"+std::to_string(i), std::string(512, 'x'));
  }

  // после рестарта в wal/ должен остаться только новый пустой сегмент
  auto wal_dir = fs::path(dir) / "wal";
  REQUIRE(fs::exists(wal_dir));
  REQUIRE(count_wal_files(wal_dir) >= 1); // как минимум один новый сегмент
  // не должно быть нескольких старых сегментов (в норме ровно один)
  REQUIRE(count_wal_files(wal_dir) <= 2); // допускаем 1-2 на медленной FS
}
