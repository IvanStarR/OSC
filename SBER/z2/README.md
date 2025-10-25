1) REQUIREMENTS & INSTALLATION
------------------------------

System requirements
- Linux x86_64 or aarch64
- CMake ≥ 3.16
- C++20 compiler (GCC ≥ 11 or Clang ≥ 14)
- Git
- Optional: liburing (enables io_uring fast path via pkg-config)

3rd-party dependencies (auto-fetched)
- fmt 10.2.1
- spdlog 1.13.0
- xxhash v0.8.2
- Catch2 v3.5.3 (tests)

Directory layout
- include/        public headers
- source/         core (KV, WAL, SST, cache, etc.)
- application/    CLI tool (binary name: uringkv)
- tests/          Catch2 unit tests
- CMakeLists.txt  top-level

Build (Debug)
  mkdir -p build
  cd build
  cmake -DCMAKE_BUILD_TYPE=Debug ..
  make -j

Binary location (from build dir)
  ./bin/uringkv


2) RUNNING THE BINARY
---------------------

Quick start (from build dir)
  ./bin/uringkv --path /tmp/uringkv_demo run
  ./bin/uringkv --path /tmp/uringkv_demo put --key foo --value bar
  ./bin/uringkv --path /tmp/uringkv_demo get --key foo
  ./bin/uringkv --path /tmp/uringkv_demo del --key foo
  ./bin/uringkv --path /tmp/uringkv_demo scan --start a --end z
  ./bin/uringkv --path /tmp/uringkv_demo metrics
  ./bin/uringkv --path /tmp/uringkv_demo metrics --watch 2

io_uring fast path (if liburing is installed)
  # enable io_uring with queue depth 256
  ./bin/uringkv --path /tmp/uringkv_demo --use-uring on --queue-depth 256 run
  # enable SQPOLL
  ./bin/uringkv --path /tmp/uringkv_demo --use-uring on --uring-sqpoll on run

Benchmark mode (example)
  ./bin/uringkv --path /tmp/uringkv_bench bench \
    --ops 100000 --threads 4 --ratio 90:5:5 \
    --key-len 16 --val-len 100 \
    --use-uring on --queue-depth 256 \
    --segment 64M --group-commit 1M \
    --flush fdatasync \
    --bg-compact on --l0-threshold 6 --table-cache 128


3) CLI & PUBLIC C++ API
-----------------------

CLI modes
  run | bench | put | get | del | scan | metrics

Common options
  --path DIR                 data dir (default /tmp/uringkv_demo)
  --use-uring on|off         enable io_uring (default off)
  --queue-depth N            io_uring QD (default 256)
  --uring-sqpoll on|off      SQPOLL (default off)
  --flush fdatasync|fsync|sfr durability (default fdatasync)
  --compaction-policy size-tiered|leveled (leveled = stub)
  --segment BYTES            WAL max segment (default 64MiB)
  --group-commit BYTES       bytes per fsync (default 1MiB)
  --flush-threshold BYTES    SST flush threshold (default 4MiB)
  --bg-compact on|off        background compaction (default on)
  --l0-threshold N           start compaction at N files (default 6)
  --table-cache N            SST table cache capacity (default 64)

KV ops
  put  --key K --value V
  get  --key K
  del  --key K
  scan --start A --end B

Bench
  --ops N            total ops (default 100000)
  --ratio P:G:D      mix in percent (default 90:5:5)
  --key-len N        default 16
  --val-len N        default 100
  --threads N        default 1

Metrics
  metrics            one-shot
  metrics --watch S  periodic deltas every S seconds


4) FEATURES OVERVIEW
--------------------
- WAL (write-ahead log) with segment rotation and 4 KiB record padding.
- SSTables (sorted):
  * Per-record trailer & checksum, padded to 4 KiB.
  * Mmap’d hash index (open addressing, LF ≤ 0.5) for point lookups.
  * Sparse index (ordered samples) to speed up range scans.
  * Versioned footer with offsets.
- Table cache (LRU) with hit/miss metrics.
- Background compaction (size-tiered; leveled policy = placeholder).
- Durability modes: fdatasync, fsync, sync_file_range (Linux).
- CLI: CRUD, range scan, micro-bench (p50/p95/p99), metrics snapshot & watch.


5) RUNNING TESTS
----------------
Build with tests (default ON)
  mkdir -p build
  cd build
  cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DURINGKV_ENABLE_TESTS=ON ..
  make -j

Run all tests
  ctest -j

Verbose
  ctest -V

Run test binary directly (from build dir)
  ./bin/uringkv_tests -l
  ./bin/uringkv_tests -t sst



