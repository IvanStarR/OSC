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
    KV kv({.path=dir, .final_flush_on_close=false});
    REQUIRE(kv.init_storage_layout());
    kv.put("ok1","111");
    kv.put("bad","222");
  }

  auto wal = std::filesystem::path(dir)/"wal"/"000001.wal";
  REQUIRE(std::filesystem::exists(wal));

  int fd = ::open(wal.c_str(), O_RDONLY);
  REQUIRE(fd >= 0);

  char header[WalSegmentConst::HEADER_SIZE];
  REQUIRE(::read(fd, header, sizeof(header)) == (ssize_t)sizeof(header));

  size_t records = 0;
  uint64_t last_used = 0;
  uint64_t last_pad  = 0;

  while (true) {
    WalRecordMeta m{};
    ssize_t r = ::read(fd, &m, sizeof(m));
    if (r == 0) break;                      // EOF
    REQUIRE(r == (ssize_t)sizeof(m));

    // тело
    if (m.klen) {
      std::vector<char> kb(m.klen);
      REQUIRE(::read(fd, kb.data(), m.klen) == (ssize_t)m.klen);
    }
    if (m.vlen) {
      std::vector<char> vb(m.vlen);
      REQUIRE(::read(fd, vb.data(), m.vlen) == (ssize_t)m.vlen);
    }

    WalRecordTrailer tr{};
    REQUIRE(::read(fd, &tr, sizeof(tr)) == (ssize_t)sizeof(tr));

    const uint64_t used = sizeof(WalRecordMeta) + m.klen + m.vlen + sizeof(WalRecordTrailer);
    const uint64_t rem  = used % WalSegmentConst::BLOCK_SIZE;
    const uint64_t pad  = rem ? (WalSegmentConst::BLOCK_SIZE - rem) : 0;

    last_used = used;
    last_pad  = pad;
    ++records;

    if (pad) ::lseek(fd, (off_t)pad, SEEK_CUR);
  }
  ::close(fd);

  REQUIRE(records >= 2);

  auto sz = std::filesystem::file_size(wal);
  REQUIRE(sz >= WalSegmentConst::HEADER_SIZE + last_used + last_pad);
  REQUIRE(::truncate(wal.c_str(), (off_t)(sz - (last_pad + 4))) == 0);

  {
    KV kv({.path=dir});
    auto v1 = kv.get("ok1");
    REQUIRE(v1.has_value());
    REQUIRE(*v1 == "111");

    REQUIRE_FALSE(kv.get("bad").has_value());
  }
}
