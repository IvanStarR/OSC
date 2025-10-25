#ifdef __has_include
#  if __has_include(<catch2/catch_all.hpp>)
#    include <catch2/catch_all.hpp>
#  else
#    include <catch2/catch.hpp>
#  endif
#endif

#include "kv.hpp"
#include "wal/segment.hpp"
#include "wal/record.hpp"

#include <filesystem>
#include <vector>
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
    // ВАЖНО: выключаем финальный flush, чтобы хвост остался в WAL
    KV kv({.path=dir, .final_flush_on_close=false});
    REQUIRE(kv.init_storage_layout());
    kv.put("ok1","111");
    kv.put("bad","222"); // эту запись будем «рвать»
  }

  auto wal = std::filesystem::path(dir)/"wal"/"000001.wal";
  REQUIRE(std::filesystem::exists(wal));

  // Разберём файл и посчитаем количество полноценных записей
  int fd = ::open(wal.c_str(), O_RDONLY);
  REQUIRE(fd >= 0);

  // пропускаем 4К заголовок
  char header[WalSegmentConst::HEADER_SIZE];
  REQUIRE(::read(fd, header, sizeof(header)) == (ssize_t)sizeof(header));

  size_t records = 0;
  off_t pos = ::lseek(fd, 0, SEEK_CUR);
  (void)pos;

  while (true) {
    WalRecordMeta m{};
    ssize_t r = ::read(fd, &m, sizeof(m));
    if (r == 0) break;
    REQUIRE(r == (ssize_t)sizeof(m));

    // прочитаем тело
    if (m.klen) {
      std::vector<char> kb(m.klen);
      REQUIRE(::read(fd, kb.data(), m.klen) == (ssize_t)m.klen);
    }
    if (m.vlen) {
      std::vector<char> vb(m.vlen);
      REQUIRE(::read(fd, vb.data(), m.vlen) == (ssize_t)m.vlen);
    }

    // трейлер
    WalRecordTrailer tr{};
    REQUIRE(::read(fd, &tr, sizeof(tr)) == (ssize_t)sizeof(tr));

    // сдвиг на паддинг
    const uint64_t used = sizeof(m) + m.klen + m.vlen + sizeof(WalRecordTrailer);
    const uint64_t rem  = used % WalSegmentConst::BLOCK_SIZE;
    if (rem) {
      ::lseek(fd, (off_t)(WalSegmentConst::BLOCK_SIZE - rem), SEEK_CUR);
    }

    ++records;
  }
  ::close(fd);

  // sanity: в журнале как минимум две записи
  REQUIRE(records >= 2);

  // повредим самый хвост: минус 8 байт (середина трейлера последней записи)
  auto sz = std::filesystem::file_size(wal);
  REQUIRE(sz > WalSegmentConst::HEADER_SIZE + 8);
  REQUIRE(::truncate(wal.c_str(), (off_t)(sz - 8)) == 0);

  // Перезапуск и проверка
  {
    KV kv({.path=dir});
    auto v1 = kv.get("ok1");
    REQUIRE(v1.has_value());
    REQUIRE(*v1 == "111");

    REQUIRE_FALSE(kv.get("bad").has_value());
  }
}
