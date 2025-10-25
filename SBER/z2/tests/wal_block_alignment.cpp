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
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

using namespace uringkv;

static std::string tmpdir(const char* prefix){
  auto base = std::filesystem::temp_directory_path();
  auto d = base / (std::string(prefix) + std::to_string(::getpid()));
  std::filesystem::create_directories(d);
  return d.string();
}

TEST_CASE("WAL: block alignment and trailer validity") {
  auto dir = tmpdir("uringkv_align_");
  {
    KV kv({.path=dir});
    REQUIRE(kv.init_storage_layout());

    // Набор разноразмерных записей
    kv.put("a", std::string(1, 'x'));
    kv.put("bb", std::string(7, 'y'));
    kv.put("ccc", std::string(3000, 'z'));
    kv.del("gone");
    kv.put(std::string(200, 'K'), std::string(200, 'V'));
  }

  auto wal = std::filesystem::path(dir)/"wal"/"000001.wal";
  REQUIRE(std::filesystem::exists(wal));

  int fd = ::open(wal.c_str(), O_RDONLY);
  REQUIRE(fd >= 0);

  // пропускаем header 4К
  char header[WalSegmentConst::HEADER_SIZE];
  REQUIRE(::read(fd, header, sizeof(header)) == (ssize_t)sizeof(header));

  // читаем записи пока не EOF
  while (true) {
    WalRecordMeta m{};
    ssize_t r = ::read(fd, &m, sizeof(m));
    if (r == 0) break;                    // EOF
    REQUIRE(r == (ssize_t)sizeof(m));     // полный meta

    std::string k, v;
    k.resize(m.klen);
    v.resize(m.vlen);
    if (m.klen)  REQUIRE(::read(fd, k.data(), m.klen) == (ssize_t)m.klen);
    if (m.vlen)  REQUIRE(::read(fd, v.data(), m.vlen) == (ssize_t)m.vlen);

    WalRecordTrailer tr{};
    REQUIRE(::read(fd, &tr, sizeof(tr)) == (ssize_t)sizeof(tr));
    const uint32_t expect_len = static_cast<uint32_t>(sizeof(m) + m.klen + m.vlen);

    REQUIRE(tr.magic   == WAL_TRAILER_MAGIC);
    REQUIRE(tr.rec_len == expect_len);

    // паддинг до 4К
    const uint64_t used = expect_len + sizeof(WalRecordTrailer);
    const uint64_t rem  = used % WalSegmentConst::BLOCK_SIZE;
    if (rem) {
      const off_t skip = (off_t)(WalSegmentConst::BLOCK_SIZE - rem);
      REQUIRE(::lseek(fd, skip, SEEK_CUR) >= 0);
    }
    // позиция должна быть на границе 4К
    off_t pos = ::lseek(fd, 0, SEEK_CUR);
    REQUIRE(pos % WalSegmentConst::BLOCK_SIZE == 0);
  }

  ::close(fd);
}
