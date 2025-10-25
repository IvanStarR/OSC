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

using namespace uringkv;
namespace fs = std::filesystem;

static std::string mktmp(const char* p){
  auto b = fs::temp_directory_path();
  auto d = b / (std::string(p) + std::to_string(::getpid()));
  fs::create_directories(d);
  return d.string();
}

static size_t count_wals(const fs::path& wal_dir){
  size_t n=0;
  if (fs::exists(wal_dir)) for (auto& e: fs::directory_iterator(wal_dir)) {
    if (e.is_regular_file() && e.path().extension()==".wal") ++n;
  }
  return n;
}

TEST_CASE("WAL aligned to 4K and trailer guards torn write") {
  auto dir = mktmp("uringkv_wal4k_");
  {
    KV kv({.path=dir});
    REQUIRE(kv.init_storage_layout());
    kv.put("a","111");
    kv.put("b","222");
    kv.put("c","333");
  }
  // проверим кратность 4096 у текущего сегмента
  auto wal_dir = fs::path(dir) / "wal";
  REQUIRE(fs::exists(wal_dir));
  fs::path wal_file;
  for (auto& e: fs::directory_iterator(wal_dir)) if (e.path().extension()==".wal") wal_file = e.path();
  REQUIRE(!wal_file.empty());
  auto sz = fs::file_size(wal_file);
  REQUIRE(sz % 4096 == 0);

  // испортим трейлер: последний непустой участок (минус 1 байт)
  {
    std::fstream f(wal_file, std::ios::in|std::ios::out|std::ios::binary);
    REQUIRE(f.good());
    auto size = static_cast<std::streamoff>(sz);
    // отнимем хотя бы 1 байт, чтобы испортить trailer/padding
    f.seekp(size - 1, std::ios::beg);
    char bad = static_cast<char>(0xEE);
    f.write(&bad, 1);
    f.flush();
  }

  // после рестарта первые записи должны сохраниться, даже если последняя испорчена
  {
    KV kv({.path=dir});
    REQUIRE(kv.get("a").value()=="111");
    REQUIRE(kv.get("b").value()=="222");
    // "c" может пропасть из-за оборванного хвоста — это и проверяем
    (void)kv.get("c");
  }
}
