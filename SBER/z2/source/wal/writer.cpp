#include "wal/writer.hpp"
#include "util.hpp"
#include <spdlog/spdlog.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>
#include <algorithm>

// ... остальной код без изменений ...

bool WalWriter::append_(const WalRecordMeta& m, std::string_view k, std::string_view v) {
  if (fd_ < 0) return false;

  const uint64_t body = sizeof(m) + k.size() + v.size();
  const uint64_t trailer_sz = sizeof(WalRecordTrailer);

  // если не помещаемся — ротируем до записи
  if (!open_or_rotate_if_needed(body + trailer_sz, m.seqno)) return false;

  WalRecordTrailer tr{};
  tr.rec_len = static_cast<uint32_t>(body);
  tr.magic   = WAL_TRAILER_MAGIC;

  struct ::iovec iov[4];
  iov[0].iov_base = const_cast<WalRecordMeta*>(&m);
  iov[0].iov_len  = sizeof(m);
  iov[1].iov_base = const_cast<char*>(k.data());
  iov[1].iov_len  = k.size();
  iov[2].iov_base = const_cast<char*>(v.data());
  iov[2].iov_len  = v.size();
  iov[3].iov_base = &tr;
  iov[3].iov_len  = sizeof(tr);

  if (!write_vectored(iov, 4)) return false;

  // паддинг до 4К
  const uint64_t used = body + trailer_sz;
  const uint64_t rem  = used % WalSegmentConst::BLOCK_SIZE;
  uint64_t pad = rem ? (WalSegmentConst::BLOCK_SIZE - rem) : 0;
  if (pad) {
    // писать нули небольшими кусками, чтобы не аллоцировать большой буфер
    char zeros[WalSegmentConst::BLOCK_SIZE] = {0};
    while (pad > 0) {
      const size_t chunk = static_cast<size_t>(std::min<uint64_t>(pad, sizeof(zeros)));
      ssize_t w = ::write(fd_, zeros, chunk);
      if (w < 0) { spdlog::error("WAL pad write failed"); return false; }
      pad -= static_cast<uint64_t>(w);
    }
  }

  seg_size_ += used + ((rem)?(WalSegmentConst::BLOCK_SIZE - rem):0);
  bytes_since_sync_ += used; // в метрику считаем «полезные» байты

  if (bytes_since_sync_ > (1<<20)) {
    if (!fsync_backend()) return false;
    bytes_since_sync_ = 0;
  }
  return true;
}
