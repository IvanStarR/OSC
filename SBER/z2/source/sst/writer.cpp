// source/sst/writer.cpp
#include "sst/writer.hpp"
#include "sst/footer.hpp"
#include "sst/record.hpp"
#include "sst/index.hpp"
#include "util.hpp"

#include <xxhash.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/uio.h>
#include <unistd.h>

namespace uringkv {

SstWriter::SstWriter(const std::string& path) : path_(path) {
  fd_ = ::open(path_.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
  if (fd_ < 0) {
    spdlog::error("SST open failed: {} (errno={})", path_, errno);
  }
}

SstWriter::~SstWriter() {
  if (fd_ >= 0) ::close(fd_);
}

// --- helpers ---
static inline uint64_t xxh64_concat(std::string_view a, std::string_view b) {
  XXH64_state_t* st = XXH64_createState();
  XXH64_reset(st, 0);
  if (!a.empty()) XXH64_update(st, a.data(), a.size());
  if (!b.empty()) XXH64_update(st, b.data(), b.size());
  const uint64_t h = static_cast<uint64_t>(XXH64_digest(st));
  XXH64_freeState(st);
  return h;
}

static inline uint64_t next_pow2(uint64_t x) {
  if (x <= 1) return 1;
  --x;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  x |= x >> 32;
  return x + 1;
}

bool SstWriter::write_sorted(
    const std::vector<std::pair<std::string, std::optional<std::string>>>& entries,
    uint32_t /*index_step*/)
{
  if (fd_ < 0) return false;

  if (!entries.empty()) {
    for (size_t i = 1; i < entries.size(); ++i) {
      if (entries[i-1].first > entries[i].first) {
        spdlog::warn("SST entries not sorted; writer will still proceed");
        break;
      }
    }
  }

  // ---- write records (data section) ----
  uint64_t offset = 0;
  for (const auto& kv : entries) {
    const std::string& k = kv.first;
    const bool is_put = kv.second.has_value();
    const std::string_view v = is_put ? std::string_view(*kv.second) : std::string_view();

    const uint64_t h = xxh64_concat(k, v);

    SstRecordMeta m{
      static_cast<uint32_t>(k.size()),
      static_cast<uint32_t>(v.size()),
      is_put ? SST_FLAG_PUT : SST_FLAG_DEL,
      h
    };

    struct ::iovec iov[3];
    iov[0].iov_base = const_cast<SstRecordMeta*>(&m);
    iov[0].iov_len  = sizeof(m);
    iov[1].iov_base = const_cast<char*>(k.data());
    iov[1].iov_len  = k.size();
    iov[2].iov_base = const_cast<char*>(v.data());
    iov[2].iov_len  = v.size();

    ssize_t w = ::writev(fd_, iov, 3);
    if (w < 0) {
      spdlog::error("SST writev failed (errno={}): {}", errno, path_);
      return false;
    }
    offset += static_cast<uint64_t>(w);
  }

  // ---- build mmap-able hash index in memory ----
  // We index every record (PUT/DEL) so lookups can return tombstones.
  uint64_t items = static_cast<uint64_t>(entries.size());
  // keep load factor <= 0.5
  uint64_t table_size = next_pow2(std::max<uint64_t>(1, items * 2));
  std::vector<HashIndexEntry> table(table_size);
  std::fill(table.begin(), table.end(), HashIndexEntry{0, 0});

  uint64_t cur_off = 0;
  for (const auto& kv : entries) {
    const std::string& k = kv.first;
    const std::string_view v = kv.second ? std::string_view(*kv.second) : std::string_view();

    // meta + key + value length to advance offset after placing entry
    const uint64_t rec_len = sizeof(SstRecordMeta) +
                             static_cast<uint64_t>(k.size()) +
                             static_cast<uint64_t>(v.size());

    uint64_t h = sst_key_hash(k.data(), k.size());
    if (h == 0) h = 1; // reserve 0 for "empty"

    const uint64_t mask = table_size - 1;
    uint64_t pos = h & mask;

    for (uint64_t step = 0; step < table_size; ++step) {
      if (table[pos].h == 0) {
        table[pos].h = h;
        table[pos].off = cur_off;
        break;
      }
      pos = (pos + 1) & mask;
    }

    cur_off += rec_len;
  }

  // ---- write index block ----
  const uint64_t index_offset = offset;

  HashIndexHeader hdr{};
  hdr.magic      = kHidxMagic;
  hdr.version    = kHidxVersion;
  hdr.table_size = table_size;
  hdr.num_items  = items;

  // header
  {
    ssize_t w = ::write(fd_, &hdr, sizeof(hdr));
    if (w != (ssize_t)sizeof(hdr)) {
      spdlog::error("SST index header write failed");
      return false;
    }
    offset += static_cast<uint64_t>(w);
  }
  // table
  {
    const char* ptr = reinterpret_cast<const char*>(table.data());
    size_t left = table.size() * sizeof(HashIndexEntry);
    while (left > 0) {
      ssize_t w = ::write(fd_, ptr, left);
      if (w < 0) {
        spdlog::error("SST index table write failed (errno={})", errno);
        return false;
      }
      ptr  += w;
      left -= static_cast<size_t>(w);
      offset += static_cast<uint64_t>(w);
    }
  }

  // ---- write footer ----
  SstFooter f{};
  f.index_offset = index_offset;
  f.index_count  = static_cast<uint32_t>(table_size); // table_size by contract
  f.version      = kSstVersion;
  std::memset(f.magic, 0, sizeof(f.magic));
  std::memcpy(f.magic, kSstMagic, 7);

  if (::write(fd_, &f, sizeof(f)) != (ssize_t)sizeof(f)) {
    spdlog::error("SST footer write failed");
    return false;
  }

  // Durability of SST file (best-effort).
  ::fsync(fd_);
  return true;
}

} // namespace uringkv
