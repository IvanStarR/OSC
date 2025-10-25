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
- xxhash v0.8.2
- Catch2 v3.5.3 (tests, optional)

Directory layout
- include/         public headers
- source/          core
- application/     CLI tool (binary name: gitproc)
- tests/           Catch2 unit tests
- CMakeLists.txt   top-level


  sudo dnf -y install cmake git gcc gcc-c++ make ninja-build pkgconf-pkg-config

Build (Debug)
  mkdir -p build
  cd build
  cmake -DCMAKE_BUILD_TYPE=Debug ..
  make -j

Binary location (from build dir)
  ./bin/gitproc


2) RUNNING THE BINARY
---------------------

Quick start (from build dir)
  # показать доступные команды и опции
  ./bin/gitproc --help

  # версия/метаданные сборки
  ./bin/gitproc --version

###############################################################################
# GitProc — Сценарий воспроизведения
# Папки: /tmp/gp_demo (сборка/логи), /tmp/gp_repo (Git)
###############################################################################

# Чистый старт
pkill -f "/tmp/gp_demo/build/bin/gitproc run" || true
rm -rf /tmp/gp_demo /tmp/gp_repo

###############################################################################
# 0) СБОРКА
###############################################################################
mkdir -p /tmp/gp_demo/build
cmake -S . -B /tmp/gp_demo/build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/gp_demo/build -j

###############################################################################
# 1) ЛОКАЛЬНЫЙ UNIT: start / status / logs / stop
###############################################################################
mkdir -p /tmp/gp_demo/services
cat > /tmp/gp_demo/services/echo.service <<'EOF'
[Service]
ExecStart=/bin/sh -c 'echo hello; for i in 1 2 3 4 5; do echo tick:$i; sleep 1; done; sleep 60'
EOF

cd /tmp/gp_demo
/tmp/gp_demo/build/bin/gitproc start services/echo.service
/tmp/gp_demo/build/bin/gitproc status services/echo.service
/tmp/gp_demo/build/bin/gitproc logs echo --lines 20
/tmp/gp_demo/build/bin/gitproc stop services/echo.service

###############################################################################
# 2) DEMON RUN + AUTO-RESTART + AUTO-SYNC (GIT)
###############################################################################
mkdir -p /tmp/gp_repo/services
cat > /tmp/gp_repo/services/restart.service <<'EOF'
[Service]
ExecStart=/bin/sh -c 'date "+start:%T"; sleep 1; echo crash; exit 1'
Restart=always
RestartSec=1
EOF
git -C /tmp/gp_repo init -q
git -C /tmp/gp_repo -c user.email=a@a -c user.name=a add .
git -C /tmp/gp_repo -c user.email=a@a -c user.name=a commit -qm "v1"

cd /tmp/gp_demo
rm -rf /tmp/gp_demo/.gitproc_work
/tmp/gp_demo/build/bin/gitproc run --repo file:///tmp/gp_repo --unit services/restart.service --autosync-sec 2 &
sleep 5
/tmp/gp_demo/build/bin/gitproc logs restart --lines 50

sed -i 's/crash/hello_v2_and_crash/' /tmp/gp_repo/services/restart.service
git -C /tmp/gp_repo -c user.email=a@a -c user.name=a add services/restart.service
git -C /tmp/gp_repo -c user.email=a@a -c user.name=a commit -qm "v2"
sleep 3
/tmp/gp_demo/build/bin/gitproc logs restart --lines 50

pkill -f "/tmp/gp_demo/build/bin/gitproc run" || true

###############################################################################
# 3) HEALTH-CHECK (HTTP) + РЕСТАРТ ПО ДЕГРАДАЦИИ
###############################################################################
cat > /tmp/gp_repo/services/health.service <<'EOF'
[Service]
ExecStart=/bin/sh -c 'echo up; sleep 2; echo down; sleep 1'
HealthHttpUrl=http://127.0.0.1:65535/
HealthHttpTimeoutMs=300
HealthHttpExpect=200-299
WatchdogSec=1
Restart=always
RestartSec=1
EOF
git -C /tmp/gp_repo -c user.email=a@a -c user.name=a add services/health.service
git -C /tmp/gp_repo -c user.email=a@a -c user.name=a commit -qm "health v1"

cd /tmp/gp_demo
rm -rf /tmp/gp_demo/.gitproc_work
/tmp/gp_demo/build/bin/gitproc run --repo file:///tmp/gp_repo --unit services/health.service --autosync-sec 2 &
sleep 5
/tmp/gp_demo/build/bin/gitproc logs health --lines 80
pkill -f "/tmp/gp_demo/build/bin/gitproc run" || true

###############################################################################
# 4) HOT RELOAD (ExecReload) 
###############################################################################
cat > /tmp/gp_repo/services/reloadable.service <<'EOF'
[Service]
ExecStart=/bin/sh -c 'trap "echo HUP >> /tmp/gp_demo/reload.log" HUP; echo start; tail -f /dev/null'
ExecReload=kill -HUP %p
EOF
git -C /tmp/gp_repo -c user.email=a@a -c user.name=a add services/reloadable.service
git -C /tmp/gp_repo -c user.email=a@a -c user.name=a commit -qm "reloadable"

cd /tmp/gp_demo
rm -rf /tmp/gp_demo/.gitproc_work
/tmp/gp_demo/build/bin/gitproc run --repo file:///tmp/gp_repo --unit services/reloadable.service &
sleep 2
/tmp/gp_demo/build/bin/gitproc reload reloadable
sleep 1
cat /tmp/gp_demo/reload.log || echo "(reload.log ещё не создан)"
pkill -f "/tmp/gp_demo/build/bin/gitproc run" || true

###############################################################################
# 5) ЛОГИ + РОТАЦИЯ (env: GITPROC_LOG_MAX_MB)
###############################################################################
cat > /tmp/gp_repo/services/spam.service <<'EOF'
[Service]
ExecStart=/bin/sh -c 'i=0; while true; do echo "line:$i $(head -c 2048 < /dev/zero | tr "\0" "A")"; i=$((i+1)); done'
EOF
git -C /tmp/gp_repo -c user.email=a@a -c user.name=a add services/spam.service
git -C /tmp/gp_repo -c user.email=a@a -c user.name=a commit -qm "spam"

cd /tmp/gp_demo
rm -rf /tmp/gp_demo/.gitproc_work
GITPROC_LOG_MAX_MB=1 /tmp/gp_demo/build/bin/gitproc run --repo file:///tmp/gp_repo --unit services/spam.service &
sleep 4
ls -lh /tmp/gp_demo/logs/spam.* || echo "(файлы ротации ещё не появились)"
pkill -f "/tmp/gp_demo/build/bin/gitproc run" || true




3) CLI & PUBLIC C++ API
-----------------------

CLI modes (may vary by build; see --help)
  init | sync | exec | supervise | status | logs | version | help

Common options
  --repo URL                 Git URL (for `init`)
  --workdir DIR              Working directory with checked-out repo
  --branch NAME              Branch to track (default: main)
  --interval DURATION        Poll interval for `supervise` (e.g. 5s, 1m)
  --on-update CMD            Hook to run when repo changes (supervise)
  --log-level lvl            trace|debug|info|warn|error (default: info)
  --log-file PATH            Write logs to file (stdout if omitted)
  --no-color                 Disable colored output

init
  init --repo URL --workdir DIR [--branch NAME]
    Clone/prepare working directory and set tracking branch.

sync
  sync --workdir DIR
    Fetch and fast-forward to remote HEAD; update submodules if present.

exec
  exec --workdir DIR -- CMD [ARGS...]
    Execute arbitrary command with CWD=DIR and exported repo metadata.

supervise
  supervise --workdir DIR [--interval 5s] [--on-update CMD]
    Periodically checks remote and runs `sync`; executes hook on changes.

status
  status --workdir DIR
    Print current branch, commit, cleanliness, and remote head.

logs
  logs --workdir DIR [--tail N]
    Show recent application logs.

Notes
  - Все подкоманды имеют `--help`, напр.: `gitproc init --help`.
  - При необходимости укажите прокси/SSH-ключи обычными средствами Git.



4) RUNNING TESTS
----------------
Build with tests (ON by default if enabled in the project)
  mkdir -p build
  cd build
  cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DURINGKV_ENABLE_TESTS=ON ..
  make -j

Run all tests
  ctest -j --output-on-failure



