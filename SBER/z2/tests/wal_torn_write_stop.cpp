#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include "wal/segment.hpp"
#include <filesystem>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace uringkv;

static std::string tdir(const char* p){
  auto b = std::filesystem::temp_directory_path();
  auto d = b / (std::string(p)+std::to_string(::getpid()));
  std::filesystem::create_directories(d);
  return d.string();
}

TEST_CASE("WAL: torn write stops replay at last record only") {
  auto dir = tdir("uringkv_torn_");

  {
    KV kv({.path=dir});
    REQUIRE(kv.init_storage_layout());
    kv.put("ok1","111");
    kv.put("bad","222"); // испортим эту
  }

  auto wal = std::filesystem::path(dir)/"wal"/"000001.wal";
  REQUIRE(std::filesystem::exists(wal));
  const auto size = std::filesystem::file_size(wal);
  REQUIRE(size > WalSegmentConst::HEADER_SIZE);

  REQUIRE(::truncate(wal.c_str(), (off_t)(size - 16)) == 0);

  {
    KV kv({.path=dir});
    // первая запись должна сохраниться
    REQUIRE(kv.get("ok1").has_value());
    REQUIRE(*kv.get("ok1") == "111");
    // вторая должна отсутствовать (обрыв)
    REQUIRE_FALSE(kv.get("bad").has_value());
  }
}
