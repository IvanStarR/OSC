
1) REQUIREMENTS & INSTALLATION
------------------------------

System requirements
- Linux x86_64 or aarch64
- CMake ≥ 3.16
- C++20 compiler (GCC ≥ 11 or Clang ≥ 14)
- Git
- OpenSSL (libssl/libcrypto)

3rd-party dependencies (auto-fetched by CMake via tar.gz)
- fmt 10.2.1
- spdlog 1.13.0
- Catch2 v3.5.3 (tests, optional)

Directory layout (example)
- include/                 public headers
- source/                  implementation (agent/server/storage/crypto/…)
- tools/                   CLI utility (binary: secmemctl)
- CMakeLists.txt           top-level build file

Install build deps (Fedora/openEuler/RHEL-like)
  sudo dnf -y install cmake git gcc gcc-c++ make ninja-build pkgconf-pkg-config openssl-devel

Build
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j

Binary location (from build dir)
  ./bin/secmem-agent
  ./bin/secmemctl


2) RUNNING THE SERVER & CLI
---------------------------

The server is a local secret agent over an AF_UNIX socket. Secrets are stored in RAM using memfd+SEAL (no disk). Data are encrypted with AES-256-GCM; the key lives in an mlock+mmap region. Plaintext is delivered to clients via SCM_RIGHTS (FD passing).

Quick start (from repo root)
  uid=$(id -u)
  ./build/bin/secmem-agent --socket /tmp/secmem.sock --allow-uid "$uid" --default-ttl 600 &

Socket permissions
  ls -l /tmp/secmem.sock
  # expected: srw-------

CLI: secmemctl
  ./build/bin/secmemctl --socket /tmp/secmem.sock COMMAND ...

Commands
  put KEY VALUE [--ttl SEC]   — store a secret with TTL (seconds). TTL=0 means no expiry.
  get KEY                      — fetch secret; prints plaintext to stdout
  del KEY                      — delete secret
  list                         — list keys visible to the current UID
  metrics                      — print Prometheus metrics (text exposition)

Examples
  ./build/bin/secmemctl --socket /tmp/secmem.sock put db_pass s3cr3t --ttl 10
  ./build/bin/secmemctl --socket /tmp/secmem.sock get db_pass
  ./build/bin/secmemctl --socket /tmp/secmem.sock del db_pass
  ./build/bin/secmemctl --socket /tmp/secmem.sock list
  ./build/bin/secmemctl --socket /tmp/secmem.sock metrics


3) SECURITY MODEL (BRIEF)
-------------------------

- Memory:
  - mlockall(MCL_CURRENT|MCL_FUTURE) on the process
  - AES-256 key in mmap+mlock region; zeroized on shutdown
  - Plaintext buffers explicitly zeroized after use
- Storage:
  - Ciphertext in memfd (RAM-only), FD sealed with F_SEAL_{SEAL,GROW,SHRINK,WRITE}
- Transfer:
  - Plaintext delivered via AF_UNIX + SCM_RIGHTS (passing a sealed memfd)
- Access control:
  - Client auth via SO_PEERCRED (uid/gid/pid)
  - UID/GID ACL and per-secret owner checks on GET/DEL
- Hardening:
  - PR_SET_NO_NEW_PRIVS=1, PR_SET_DUMPABLE=0
  - UMask=0077, socket 0600, socket directory 0700
- TTL:
  - Background sweeper removes expired secrets and logs the event


4) METRICS (Prometheus text format)
-----------------------------------

Command
  ./build/bin/secmemctl --socket /tmp/secmem.sock metrics

Sample output
  secmem_ops_total{op="put"} 3
  secmem_ops_total{op="get"} 2
  secmem_errors_total{type="get"} 1
  secmem_secrets_gauge 1
  # HELP secmem_latency_seconds Request latency in seconds
  secmem_latency_seconds_bucket{op="get",le="0.001000"} 2
  secmem_latency_seconds_bucket{op="get",le="+Inf"} 2
  secmem_latency_seconds_sum{op="get"} 0.000321000
  secmem_latency_seconds_count{op="get"} 2

