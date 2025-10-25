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
    // Финальный флаш выключен — эмулируем power loss: хвост остаётся в WAL.
    KV kv({.path=dir, .final_flush_on_close=false});
    REQUIRE(kv.init_storage_layout());
    kv.put("ok1","111");
    kv.put("bad","222"); // испортим эту последнюю запись
  }

  auto wal = std::filesystem::path(dir)/"wal"/"000001.wal";
  REQUIRE(std::filesystem::exists(wal));
  const auto size = std::filesystem::file_size(wal);
  REQUIRE(size > WalSegmentConst::HEADER_SIZE);

  // Укорачиваем файл на 16 байт — попадаем в трейлер/паддинг последней записи
  REQUIRE(::truncate(wal.c_str(), (off_t)(size - 16)) == 0);

  {
    // Перезапуск БД с настройками по умолчанию (финальный флаш включён)
    KV kv({.path=dir});
    // первая запись должна сохраниться
    auto v1 = kv.get("ok1");
    REQUIRE(v1.has_value());
    REQUIRE(*v1 == "111");
    // вторая должна отсутствовать (обрыв)
    REQUIRE_FALSE(kv.get("bad").has_value());
  }
}
