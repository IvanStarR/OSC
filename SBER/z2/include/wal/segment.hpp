#pragma once
#include <cstdint>
#include <string>

namespace uringkv {

struct WalSegmentHeader {
  char     magic[8];     // "URKVWAL"
  uint32_t version;      // 1
  uint32_t reserved;     // 0
  uint64_t start_seqno;  // seqno первой записи в сегменте
};

struct WalSegmentConst {
  static constexpr const char* MAGIC = "URKVWAL";
  static constexpr uint32_t VERSION = 1;
  static constexpr size_t   HEADER_SIZE = 4096;
};

// имя файла сегмента: 000001.wal
std::string wal_segment_name(uint64_t index);

} // namespace uringkv
