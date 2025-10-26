1) REQUIREMENTS & INSTALLATION
------------------------------

System requirements
- Linux x86_64 or aarch64
- CMake ≥ 3.16
- C++20 compiler (GCC ≥ 11 or Clang ≥ 14)
- Git

3rd-party dependencies (auto-fetched by CMake via tar.gz)
- fmt 10.2.1
- spdlog 1.13.0
- xxHash 0.8.2
- asio 1.28.x (header-only)
- Catch2 v3.5.3 (tests, optional)

Directory layout
- include/         public headers
- source/          core library (gitconfig.core)
- application/     CLI tool + HTTP server (binary name: gitconfig)
- tests/           Catch2 unit tests
- CMakeLists.txt   top-level

Install toolchain (example for Fedora)
  sudo dnf -y install cmake git gcc gcc-c++ make ninja-build pkgconf-pkg-config

Build
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j

Binary location (from build dir)
  ./bin/gitconfig
Tests location (from build dir)
  ./test/smoke.test


2) RUNNING THE BINARY
---------------------

Quick start (from build dir)
  ./bin/gitconfig init --repo /tmp/gc_repo
  ./bin/gitconfig set  --repo /tmp/gc_repo /app/title hello
  ./bin/gitconfig get  --repo /tmp/gc_repo /app/title

Help
  ./bin/gitconfig  # shows usage


3) CLI
------

gitconfig init --repo <PATH> [--data-root config]
  initialize a Git-backed KV repository

gitconfig set <KEY> <VALUE> --repo <PATH> [--data-root config]
  set key value (commits change)

gitconfig get <KEY> [--commit SHA] [--default VAL] --repo <PATH> [--data-root config]
  get latest value or value at commit; prints VAL and exits 0 if --default is given and key is missing, otherwise prints 'key not found' and exits 3

gitconfig exists <KEY> --repo <PATH> [--data-root config]
  prints 1 (exit 0) if exists, else 0 (exit 4)

gitconfig delete <KEY> --repo <PATH> [--data-root config]
  delete key (commits change)

gitconfig list [PREFIX] --repo <PATH> [--data-root config] [--recursive] [--json]
  list keys under PREFIX (default "/"); sorted; prints JSON array if --json is specified

gitconfig history <KEY> [--limit N] --repo <PATH> [--data-root config]
  print "<SHA> <UNIX_TS>" history lines for the key

gitconfig remote set --repo <PATH> --remote <NAME> --url <URL>
  configure a Git remote

gitconfig push --repo <PATH> [--remote origin] [--branch main]
gitconfig pull --repo <PATH> [--remote origin] [--branch main]

gitconfig sync --repo <PATH> [--remote origin] [--branch main]
  multi-remote sync with LWW conflict resolution
  - auto-heals state:
    - if MERGE_HEAD exists → aborts merge then proceeds
    - if local HEAD is empty → fetches and creates local branch from <remote>/branch
  - aggregates result as JSON: {"synced":[...], "skipped":[...], "errors":[...]}

gitconfig node add-peer --repo <PATH> --url <GIT-URL>
  add a peer URL into config/_cluster/peers and ensure a deterministic git remote for it

gitconfig node list-peers --repo <PATH>
  list configured peer URLs

gitconfig serve --repo <PATH> [--data-root config] [--addr 0.0.0.0] [--port 8080] [--remote origin] [--branch main] [--sync-interval N]
  run built-in HTTP API with optional background auto-sync every N seconds


4) HTTP API
-----------

Base: http://<addr>:<port>

GET  /keys/<url-encoded key>            → 200 text/plain or 404
POST /keys/<url-encoded key>            (raw body = value) → 200 "OK"
DELETE /keys/<url-encoded key>          → 200 "OK"
GET  /list?prefix=<url-enc>&recursive=1 → 200 application/json [{"key":"...","dir":true|false}, ...]
POST /sync                              → 200 application/json {"synced":[...],"skipped":[...],"errors":[...]}
GET  /nodes                              → 200 application/json ["git-url-1","git-url-2"]
POST /nodes                              (raw body = git-url) → 200 "OK"

Notes
- Keys should start with "/" (e.g. /app/db/host). The server accepts URL-encoded keys.


5) EXAMPLES
-----------

Single node
  ./bin/gitconfig init --repo /tmp/gc_repo
  ./bin/gitconfig set  --repo /tmp/gc_repo /app/db/host localhost
  ./bin/gitconfig get  --repo /tmp/gc_repo /app/db/host
  ./bin/gitconfig list --repo /tmp/gc_repo / --recursive --json

Versioning
  ./bin/gitconfig set --repo /tmp/gc_repo /app/key v1
  ./bin/gitconfig set --repo /tmp/gc_repo /app/key v2
  ./bin/gitconfig history --repo /tmp/gc_repo /app/key --limit 5
  SHA_OLD=$(./bin/gitconfig history --repo /tmp/gc_repo /app/key --limit 5 | tail -n1 | awk '{print $1}')
  ./bin/gitconfig get --repo /tmp/gc_repo /app/key --commit "$SHA_OLD"

Two nodes with a central bare repo
  rm -rf /tmp/gc_bare /tmp/node1 /tmp/node2
  git -C /tmp/gc_bare init --bare
  ./bin/gitconfig init --repo /tmp/node1
  ./bin/gitconfig init --repo /tmp/node2
  ./bin/gitconfig remote set --repo /tmp/node1 --remote origin --url /tmp/gc_bare
  ./bin/gitconfig remote set --repo /tmp/node2 --remote origin --url /tmp/gc_bare
  ./bin/gitconfig set  --repo /tmp/node1 /app/a 1
  ./bin/gitconfig push --repo /tmp/node1 --remote origin --branch main
  ./bin/gitconfig serve --repo /tmp/node1 --addr 127.0.0.1 --port 8081 --remote origin --branch main --sync-interval 5 &
  ./bin/gitconfig serve --repo /tmp/node2 --addr 127.0.0.1 --port 8082 --remote origin --branch main --sync-interval 5 &
  curl -sS -X POST --data-binary '/tmp/node1' http://127.0.0.1:8082/nodes
  curl -sS -X POST --data-binary '/tmp/node2' http://127.0.0.1:8081/nodes
  curl -sS -X POST http://127.0.0.1:8082/sync
  curl -sS     http://127.0.0.1:8082/keys/%2Fapp%2Fa

Peer-to-peer without origin
  rm -rf /tmp/node1 /tmp/node2
  ./bin/gitconfig init --repo /tmp/node1
  ./bin/gitconfig init --repo /tmp/node2
  ./bin/gitconfig set  --repo /tmp/node1 /app/a 1
  ./bin/gitconfig serve --repo /tmp/node1 --addr 127.0.0.1 --port 8081 --remote "" --branch main --sync-interval 5 &
  ./bin/gitconfig serve --repo /tmp/node2 --addr 127.0.0.1 --port 8082 --remote "" --branch main --sync-interval 5 &
  curl -sS -X POST --data-binary '/tmp/node1' http://127.0.0.1:8082/nodes
  curl -sS -X POST --data-binary '/tmp/node2' http://127.0.0.1:8081/nodes
  curl -sS -X POST http://127.0.0.1:8082/sync
  curl -sS     http://127.0.0.1:8082/keys/%2Fapp%2Fa


6) RUNNING TESTS
----------------
Build with tests (enabled if Catch2 fetched)
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
  cmake --build build -j

Run all tests
  cd build
  ctest -j --output-on-failure