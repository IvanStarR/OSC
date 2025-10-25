cmake_minimum_required(VERSION 3.16)
project(rpm_graph_fast LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------- fmt + spdlog через FetchContent ----------
include(FetchContent)

FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)

# ---------- системные tinyxml2 + libsolv ----------
find_package(TinyXML2 REQUIRED)

# через pkg-config — самый надёжный способ для libsolv
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSOLV REQUIRED IMPORTED_TARGET libsolv libsolvext)

# ---------- ваш таргет ----------
add_executable(rpm_graph_fast
  src/main.cpp
  src/exec.cpp
  src/thread_pool.cpp
  src/cache.cpp
  src/repoquery.cpp       # если оставили старый backend
  src/graph.cpp
  src/json_writer.cpp
  src/solv_backend.cpp    # libsolv backend
)
target_include_directories(rpm_graph_fast PRIVATE include)

target_link_libraries(rpm_graph_fast
  PRIVATE
    spdlog::spdlog_header_only
    fmt::fmt
    TinyXML2::TinyXML2
    PkgConfig::LIBSOLV           # даёт и libsolv, и libsolvext
)

# (опционально) приглушить логи в Release
if (CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(rpm_graph_fast PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()
