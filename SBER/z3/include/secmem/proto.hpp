#pragma once
#include <cstdint>
enum Op : uint8_t { OP_PUT=1, OP_GET=2, OP_DEL=3, OP_LIST=4, OP_METRICS=5 };
struct MsgHdr { uint8_t op; uint32_t klen; uint32_t vlen; uint32_t ttl; };
struct RespHdr { uint32_t code; uint32_t n; };