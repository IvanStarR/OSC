#pragma once
#include <cstdint>
#include <string>

namespace uringkv {

// Заголовок сегмента WAL (пишется ровно 4096 байт)
struct WalSegmentHeader {
  char     magic[8];     // "URKVWAL"
  uint32_t version;      // 1
  uint32_t reserved;     // 0
  uint64_t start_seqno;  // seqno первой записи в сегменте
  // дальше padding до 4096
};

struct WalSegmentConst {
  static constexpr const char* MAGIC = "URKVWAL";
  static constexpr uint32_t VERSION = 1;
  static constexpr size_t   HEADER_SIZE = 4096;
  static constexpr size_t   BLOCK_SIZE  = 4096;   // НОВОЕ: выравнивание записей
};

// Трейлер каждой записи для детекта torn-write и расчёта паддинга
struct WalRecordTrailer {
  uint32_t rec_len;     // sizeof(meta)+klen+vlen
  uint32_t magic;       // константа для валидации
};
static constexpr uint32_t WAL_TRAILER_MAGIC = 0x57414C52u; // 'WALR'

// имя файла сегмента: 000001.wal
std::string wal_segment_name(uint64_t index);

} // namespace uringkv
