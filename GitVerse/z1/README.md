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
- Catch2 v3.5.3 (tests, optional)

Directory layout
- include/         public headers
- source/          core
- application/     CLI tool (binary name: sysaudit)
- tests/           Catch2 unit tests
- CMakeLists.txt   top-level


  sudo dnf -y install cmake git gcc gcc-c++ make ninja-build pkgconf-pkg-config

Build
  cmake -S . -B build
  cmake --build build -j

Binary location (from build dir)
  ./bin/sysaudit
Tests location (from build dir)
  ./test/smoke.test

2) RUNNING THE BINARY
---------------------

Quick start (from build dir)
  # показать доступные команды и опции
  ./bin/sysaudit --help

  # подготовить директорию и запустить наблюдение
  mkdir -p /tmp/wd
  echo "hello" > /tmp/wd/a.txt

  ./build/bin/sysaudit --watch /tmp/wd --verbose

  # Дебаунс + периодическая статистика
  build/bin/sysaudit --watch /tmp/wd --debounce-ms 300 --stats 5 --verbose

  # Пакетные коммиты: по 20 путей или раз в 2 секунды (что раньше)
  build/bin/sysaudit --watch /tmp/wd --batch-count 20 --batch-window-ms 2000 --stats 2

  # Снимок текущего дерева перед стартом
  build/bin/sysaudit --watch /tmp/wd --initial-snapshot

  # Логирование в файл с ротацией
  build/bin/sysaudit --watch /tmp/wd \
  --log-file /var/log/sysaudit.log \
  --log-rotate-max 10485760 \
  --log-rotate-files 5 \
  --stats 5 --verbose


3) CLI
-----------------------

sysaudit --watch <DIR>
         [--ignore-ext ".tmp,.swp,.log,~"]
         [--exclude PATTERN ...] [--include PATTERN ...] [--ignore-file PATH]
         [--debounce-ms N]
         [--batch-count N] [--batch-window-ms N]
         [--stats [SEC]]
         [--initial-snapshot]
         [--log-file PATH] [--log-rotate-max BYTES] [--log-rotate-files N]
         [--verbose]

          --watch <DIR> — корень наблюдения (обязателен)
          Фильтры:
            --exclude PATTERN — исключить (как в .gitignore). Можно несколько.
            --include PATTERN — принудительно включить (эквивалент !PATTERN).
            --ignore-file PATH — загрузить правила из файла; если не задан, автоматически читается <DIR>/.sysauditignore при наличии.
            Примечание: всё под .git/ игнорируется всегда.
          Дебаунс/батч:
            --debounce-ms — схлопывать всплеск событий по одному пути.
            --batch-count, --batch-window-ms — коммитить пачкой по счётчику или таймеру
          Наблюдаемость:
            --stats [SEC] — периодический вывод счётчиков.
            --log-file, --log-rotate-max, --log-rotate-files, --verbose.
          Прочее:
            --initial-snapshot — сделать единоразовый коммит состояния перед стартом.



4) RUNNING TESTS
----------------
Build with tests (ON by default if enabled in the project)
  mkdir -p build
  cd build
  make -j

Run all tests
  ctest -j --output-on-failure


