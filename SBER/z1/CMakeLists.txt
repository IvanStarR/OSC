cmake_minimum_required(VERSION 3.16)
project(rpm_graph_fast LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------
# fmt + spdlog (FetchContent)
# -----------------------------
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

# -----------------------------
# tinyxml2: ручной импорт системной библиотеки
# (можно переопределить путями: -DTINYXML2_INCLUDE_DIR=... -DTINYXML2_LIBRARY=...)
# -----------------------------
if (NOT DEFINED TINYXML2_INCLUDE_DIR)
  find_path(TINYXML2_INCLUDE_DIR
    NAMES tinyxml2.h
    PATHS /usr/include /usr/local/include
  )
endif()
if (NOT DEFINED TINYXML2_LIBRARY)
  find_library(TINYXML2_LIBRARY
    NAMES tinyxml2
    PATHS /usr/lib64 /usr/lib /usr/local/lib64 /usr/local/lib
  )
endif()

if (NOT TINYXML2_INCLUDE_DIR OR NOT TINYXML2_LIBRARY)
  message(FATAL_ERROR
    "tinyxml2 not found.\n"
    "  Expected header: /usr/include/tinyxml2.h\n"
    "  Expected lib:    /usr/lib64/libtinyxml2.so (или /usr/lib)\n"
    "Укажи пути вручную: -DTINYXML2_INCLUDE_DIR=<...> -DTINYXML2_LIBRARY=<...>")
endif()

add_library(TinyXML2::TinyXML2 UNKNOWN IMPORTED)
set_target_properties(TinyXML2::TinyXML2 PROPERTIES
  IMPORTED_LOCATION             "${TINYXML2_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${TINYXML2_INCLUDE_DIR}"
)
message(STATUS "Using system tinyxml2: inc=${TINYXML2_INCLUDE_DIR}, lib=${TINYXML2_LIBRARY}")

# -----------------------------
# libsolv + libsolvext: ручной импорт
# (можно переопределить: -DLIBSOLV_INCLUDE_DIR=... -DLIBSOLV_LIBRARY=... -DLIBSOLVEXT_LIBRARY=...)
# -----------------------------
if (NOT DEFINED LIBSOLV_INCLUDE_DIR)
  find_path(LIBSOLV_INCLUDE_DIR
    NAMES solv/pool.h
    PATHS /usr/include /usr/local/include
  )
endif()
if (NOT DEFINED LIBSOLV_LIBRARY)
  find_library(LIBSOLV_LIBRARY
    NAMES solv
    PATHS /usr/lib64 /usr/lib /usr/local/lib64 /usr/local/lib
  )
endif()
if (NOT DEFINED LIBSOLVEXT_LIBRARY)
  find_library(LIBSOLVEXT_LIBRARY
    NAMES solvext
    PATHS /usr/lib64 /usr/lib /usr/local/lib64 /usr/local/lib
  )
endif()

if (NOT LIBSOLV_INCLUDE_DIR OR NOT LIBSOLV_LIBRARY OR NOT LIBSOLVEXT_LIBRARY)
  message(FATAL_ERROR
    "libsolv/libsolvext not found.\n"
    "  Headers: /usr/include/solv/*.h\n"
    "  Libs:    /usr/lib64/libsolv.so и /usr/lib64/libsolvext.so (или /usr/lib)\n"
    "Укажи пути вручную: -DLIBSOLV_INCLUDE_DIR=... -DLIBSOLV_LIBRARY=... -DLIBSOLVEXT_LIBRARY=...")
endif()

add_library(LIBSOLV::libsolv INTERFACE IMPORTED)
target_include_directories(LIBSOLV::libsolv INTERFACE "${LIBSOLV_INCLUDE_DIR}")
target_link_libraries(LIBSOLV::libsolv INTERFACE "${LIBSOLV_LIBRARY}" "${LIBSOLVEXT_LIBRARY}")
message(STATUS "Using system libsolv: inc=${LIBSOLV_INCLUDE_DIR}, lib=${LIBSOLV_LIBRARY}, libext=${LIBSOLVEXT_LIBRARY}")

# -----------------------------
# Исполняемый файл проекта
# -----------------------------
add_executable(rpm_graph_fast
  src/main.cpp
  src/exec.cpp
  src/thread_pool.cpp
  src/cache.cpp
  src/repoquery.cpp          # оставь/убери по необходимости
  src/graph.cpp
  src/json_writer.cpp
  src/solv_backend.cpp       # libsolv backend
)
target_include_directories(rpm_graph_fast PRIVATE include)

target_link_libraries(rpm_graph_fast
  PRIVATE
    spdlog::spdlog_header_only
    fmt::fmt
    TinyXML2::TinyXML2
    LIBSOLV::libsolv
)

# приглушить логи в Release
if (CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(rpm_graph_fast PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()
