#pragma once
#include <cstdint>
#include <string>

namespace uringkv {

struct WalSegmentHeader {
  char     magic[8];   
  uint32_t version;      
  uint32_t reserved;     
  uint64_t start_seqno;  
};

struct WalSegmentConst {
  static constexpr const char* MAGIC = "URKVWAL";
  static constexpr uint32_t VERSION = 1;
  static constexpr size_t   HEADER_SIZE = 4096;
};

std::string wal_segment_name(uint64_t index);

} // namespace uringkv
