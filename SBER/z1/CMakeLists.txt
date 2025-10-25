cmake_minimum_required(VERSION 3.16)
project(rpm_graph_fast LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------- fmt + spdlog ----------
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

# ---------- tinyxml2: CONFIG -> manual -> Fetch fallback ----------
set(TINYXML2_TARGET "")
# 1) конфиг-режим (если пакет кладёт *Config.cmake)
find_package(TinyXML2 QUIET CONFIG)
if (TinyXML2_FOUND)
  set(TINYXML2_TARGET TinyXML2::TinyXML2)
else()
  find_package(tinyxml2 QUIET CONFIG)
  if (tinyxml2_FOUND)
    set(TINYXML2_TARGET tinyxml2::tinyxml2)
  else()
    # 2) ручной поиск заголовка/библиотеки
    find_path(TINYXML2_INCLUDE_DIR tinyxml2.h PATH_SUFFIXES tinyxml2)
    find_library(TINYXML2_LIBRARY NAMES tinyxml2)
    if (TINYXML2_INCLUDE_DIR AND TINYXML2_LIBRARY)
      add_library(TINYXML2_IMPORTED UNKNOWN IMPORTED)
      set_target_properties(TINYXML2_IMPORTED PROPERTIES
        IMPORTED_LOCATION             "${TINYXML2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TINYXML2_INCLUDE_DIR}"
      )
      set(TINYXML2_TARGET TINYXML2_IMPORTED)
      message(STATUS "Using system tinyxml2 (manual): inc=${TINYXML2_INCLUDE_DIR}, lib=${TINYXML2_LIBRARY}")
    else()
      # 3) фолбэк: тянем исходники tinyxml2
      message(STATUS "System tinyxml2 not found -> fetching tinyxml2...")
      FetchContent_Declare(tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG 10.0.0
      )
      FetchContent_MakeAvailable(tinyxml2)
      set(TINYXML2_TARGET tinyxml2::tinyxml2)
    endif()
  endif()
endif()

# ---------- libsolv: без pkg-config (ручной поиск) ----------
# Требуется установленный libsolv-devel (у тебя уже стоит)
find_library(LIBSOLV_LIBRARY    NAMES solv)
find_library(LIBSOLVEXT_LIBRARY NAMES solvext)
find_path(LIBSOLV_INCLUDE_DIR   solv/pool.h)
if (NOT (LIBSOLV_LIBRARY AND LIBSOLVEXT_LIBRARY AND LIBSOLV_INCLUDE_DIR))
  message(FATAL_ERROR "libsolv not found. Проверяй: libsolv-devel установлен? 
  Ожидаю libsolv.so, libsolvext.so и заголовки в /usr/include/solv")
endif()
add_library(LIBSOLV_IMPORTED INTERFACE)
target_include_directories(LIBSOLV_IMPORTED INTERFACE "${LIBSOLV_INCLUDE_DIR}")
target_link_libraries(LIBSOLV_IMPORTED INTERFACE "${LIBSOLV_LIBRARY}" "${LIBSOLVEXT_LIBRARY}")

# ---------- таргет проекта ----------
add_executable(rpm_graph_fast
  src/main.cpp
  src/exec.cpp
  src/thread_pool.cpp
  src/cache.cpp
  src/repoquery.cpp
  src/graph.cpp
  src/json_writer.cpp
  src/solv_backend.cpp
)
target_include_directories(rpm_graph_fast PRIVATE include)

target_link_libraries(rpm_graph_fast
  PRIVATE
    spdlog::spdlog_header_only
    fmt::fmt
    ${TINYXML2_TARGET}
    LIBSOLV_IMPORTED
)

if (CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(rpm_graph_fast PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()
