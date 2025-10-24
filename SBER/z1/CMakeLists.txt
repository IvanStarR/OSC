cmake_minimum_required(VERSION 3.16)
project(rpm_graph_fast LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------- fmt ----------
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

# ---------- spdlog (используем внешний fmt) ----------
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# ---------- tinyxml2 ----------
FetchContent_Declare(tinyxml2
  GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
  GIT_TAG 10.0.0
)
FetchContent_MakeAvailable(tinyxml2)

# ---------- libsolv (втягиваем исходники и собираем как сабпроект) ----------
# Требует: zlib, liblzma (xz), libxml2 в системе
FetchContent_Declare(libsolv
  GIT_REPOSITORY https://github.com/openSUSE/libsolv.git
  GIT_TAG 0.7.29   # или актуальная стабильная
)
# Параметры сборки libsolv
set(ENABLE_RPMMD ON  CACHE BOOL "" FORCE)  # чтение repodata (primary/filelists/other)
set(ENABLE_STATIC OFF CACHE BOOL "" FORCE)
# при необходимости можно включить NARROWPROTO/DEBIAN/ARCH — нам не надо
FetchContent_MakeAvailable(libsolv)

# После MakeAvailable у нас есть цели 'solv' и 'solvext'
# (они слинкуются с системными zlib/liblzma/libxml2)

# ---------- Твой исполняемый файл ----------
add_executable(rpm_graph_fast
  src/main.cpp
  src/exec.cpp
  src/thread_pool.cpp
  src/cache.cpp
  src/repoquery.cpp       # если оставляешь старый backend
  src/graph.cpp
  src/json_writer.cpp
  src/solv_backend.cpp    # libsolv backend
)
target_include_directories(rpm_graph_fast PRIVATE include)

target_link_libraries(rpm_graph_fast
  PRIVATE
    # наши фетч-зависимости
    spdlog::spdlog_header_only
    fmt::fmt
    tinyxml2::tinyxml2
    # библиотеки libsolv из сабпроекта
    solv
    solvext
)

if (CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()
