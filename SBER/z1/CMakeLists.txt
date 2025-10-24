cmake_minimum_required(VERSION 3.16)
project(rpm_graph_fast LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------
# Fetch third-party: fmt, spdlog, tinyxml2
# -----------------------------
include(FetchContent)

# fmt
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

# spdlog (используем внешний fmt)
set(SPDLOG_FMT_EXTERNAL ON)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# tinyxml2
FetchContent_Declare(tinyxml2
  GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
  GIT_TAG 10.0.0
)
FetchContent_MakeAvailable(tinyxml2)

# -----------------------------
# libsolv из системы (pkg-config)
# -----------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSOLV REQUIRED IMPORTED_TARGET libsolv libsolvext)

# -----------------------------
# Твой таргет
# -----------------------------
add_executable(rpm_graph_fast
  src/main.cpp
  src/exec.cpp
  src/thread_pool.cpp
  src/cache.cpp
  src/repoquery.cpp      # можно оставить/удалить, если не нужен
  src/graph.cpp
  src/json_writer.cpp
  src/solv_backend.cpp   # libsolv backend
)
target_include_directories(rpm_graph_fast PRIVATE include)

target_link_libraries(rpm_graph_fast
  PRIVATE
    spdlog::spdlog_header_only   # лёгкая цель spdlog
    fmt::fmt
    tinyxml2::tinyxml2
    PkgConfig::LIBSOLV           # libsolv + libsolvext
)

# Для более «тихой» сборки зависимостей:
if (CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()
