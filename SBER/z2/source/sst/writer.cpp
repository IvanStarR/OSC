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

static inline uint64_t roundup_4k(uint64_t n) {
  const uint64_t rem = n % SST_BLOCK_SIZE;
  return rem ? (n + (SST_BLOCK_SIZE - rem)) : n;
}

// ---- Sparse index on-disk structures (ordered, for fast scan) ----
struct SparseIndexHeader {
  uint32_t magic;   // 'SIDX' = 0x53494458
  uint32_t version; // 1
  uint32_t count;   // number of entries
};
static constexpr uint32_t kSparseMagic   = 0x53494458u; // 'SIDX'
static constexpr uint32_t kSparseVersion = 1u;

bool SstWriter::write_sorted(
    const std::vector<std::pair<std::string, std::optional<std::string>>>& entries,
    uint32_t index_step)
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
  if (index_step == 0) index_step = 64;

  // ---- write records (data section) ----
  uint64_t file_off = 0;
  std::vector<uint64_t> rec_offsets;
  rec_offsets.reserve(entries.size());

  // also build sparse samples for fast scan lower_bound
  std::vector<std::pair<std::string,uint64_t>> sparse;
  sparse.reserve(entries.size() / index_step + 4);

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& kv = entries[i];
    const std::string& k = kv.first;
    const bool is_put = kv.second.has_value();
    const std::string_view v = is_put ? std::string_view(*kv.second) : std::string_view();

    // record start offset for index
    rec_offsets.push_back(file_off);
    if (i % index_step == 0) sparse.emplace_back(k, file_off);

    const uint64_t h = xxh64_concat(k, v);

    SstRecordMeta m{
      static_cast<uint32_t>(k.size()),
      static_cast<uint32_t>(v.size()),
      is_put ? SST_FLAG_PUT : SST_FLAG_DEL,
      h
    };

    // write meta + key + value
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
    const uint64_t body = static_cast<uint64_t>(w);
    const uint64_t expect_body = sizeof(SstRecordMeta) + k.size() + v.size();
    if (body != expect_body) {
      spdlog::error("SST writev short write: {} < {}", body, expect_body);
      return false;
    }

    // trailer
    SstRecordTrailer tr{};
    tr.rec_len = static_cast<uint32_t>(expect_body);
    tr.magic   = SST_TRAILER_MAGIC;

    if (::write(fd_, &tr, sizeof(tr)) != (ssize_t)sizeof(tr)) {
      spdlog::error("SST write trailer failed");
      return false;
    }

    // pad to 4KiB boundary
    const uint64_t used = body + sizeof(SstRecordTrailer);
    const uint64_t padded = roundup_4k(used);
    uint64_t pad = padded - used;
    if (pad) {
      static const char zeros[4096] = {0};
      while (pad > 0) {
        const size_t chunk = static_cast<size_t>(std::min<uint64_t>(pad, sizeof(zeros)));
        ssize_t pw = ::write(fd_, zeros, chunk);
        if (pw < 0) { spdlog::error("SST pad write failed"); return false; }
        pad -= static_cast<uint64_t>(pw);
      }
    }
    file_off += padded;
  }

  // ensure last key is included for sparse tail anchor
  if (!entries.empty() && (sparse.empty() || sparse.back().first != entries.back().first)) {
    sparse.emplace_back(entries.back().first, rec_offsets.back());
  }

  // ---- build mmap-able hash index in memory ----
  const uint64_t items = static_cast<uint64_t>(entries.size());
  const uint64_t table_size = next_pow2(std::max<uint64_t>(1, items * 2)); // load factor <= 0.5
  std::vector<HashIndexEntry> table(table_size);
  std::fill(table.begin(), table.end(), HashIndexEntry{0, 0});

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& k = entries[i].first;
    uint64_t h = sst_key_hash(k.data(), k.size());
    if (h == 0) h = 1; // reserve 0 for "empty"
    const uint64_t mask = table_size - 1;
    uint64_t pos = h & mask;

    for (uint64_t step = 0; step < table_size; ++step) {
      if (table[pos].h == 0) {
        table[pos].h   = h;
        table[pos].off = rec_offsets[i]; // start of record
        break;
      }
      pos = (pos + 1) & mask;
    }
  }

  // ---- write hash index block ----
  const uint64_t index_offset = file_off;

  HashIndexHeader hdr{};
  hdr.magic      = kHidxMagic;
  hdr.version    = kHidxVersion;
  hdr.table_size = table_size;
  hdr.num_items  = items;

  {
    ssize_t w = ::write(fd_, &hdr, sizeof(hdr));
    if (w != (ssize_t)sizeof(hdr)) {
      spdlog::error("SST index header write failed");
      return false;
    }
    file_off += static_cast<uint64_t>(w);
  }
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
      file_off += static_cast<uint64_t>(w);
    }
  }

  // ---- write sparse index block (ordered keys every index_step) ----
  {
    SparseIndexHeader sh{};
    sh.magic   = kSparseMagic;
    sh.version = kSparseVersion;
    sh.count   = static_cast<uint32_t>(sparse.size());

    if (::write(fd_, &sh, sizeof(sh)) != (ssize_t)sizeof(sh)) {
      spdlog::error("SST sparse index header write failed");
      return false;
    }
    for (auto& e : sparse) {
      uint32_t klen = static_cast<uint32_t>(e.first.size());
      uint64_t off  = e.second;
      if (::write(fd_, &klen, sizeof(klen)) != (ssize_t)sizeof(klen)) return false;
      if (::write(fd_, &off,  sizeof(off))  != (ssize_t)sizeof(off))  return false;
      if (klen && ::write(fd_, e.first.data(), klen) != (ssize_t)klen) return false;
    }
  }

  // ---- write footer ----
  SstFooter f{};
  f.index_offset = index_offset;                 // points to hash-index header
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
