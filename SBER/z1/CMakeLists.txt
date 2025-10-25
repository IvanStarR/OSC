cmake_minimum_required(VERSION 3.16)
project(rpm_graph_fast LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---------- fmt + spdlog через FetchContent ----------
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

# ---------- TinyXML2: пробуем все варианты + фолбэк ----------
# 1) find_package(TinyXML2 CONFIG)
set(TINYXML2_TARGET "")
find_package(TinyXML2 QUIET)
if (TinyXML2_FOUND)
  set(TINYXML2_TARGET TinyXML2::TinyXML2)
else()
  # 2) find_package(tinyxml2 CONFIG)
  find_package(tinyxml2 QUIET CONFIG)
  if (tinyxml2_FOUND)
    set(TINYXML2_TARGET tinyxml2::tinyxml2)
  else()
    # 3) ручной поиск (dev-пакет установлен, но без config-файла)
    find_path(TINYXML2_INCLUDE_DIR tinyxml2.h PATH_SUFFIXES tinyxml2)
    find_library(TINYXML2_LIBRARY NAMES tinyxml2)
    if (TINYXML2_INCLUDE_DIR AND TINYXML2_LIBRARY)
      add_library(TINYXML2_IMPORTED UNKNOWN IMPORTED)
      set_target_properties(TINYXML2_IMPORTED PROPERTIES
        IMPORTED_LOCATION             "${TINYXML2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TINYXML2_INCLUDE_DIR}"
      )
      set(TINYXML2_TARGET TINYXML2_IMPORTED)
      message(STATUS "Using system tinyxml2 (manual): ${TINYXML2_LIBRARY}")
    else()
      # 4) последний фолбэк — подтянуть исходники
      message(STATUS "System tinyxml2 not found, fetching tinyxml2...")
      FetchContent_Declare(tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG 10.0.0
      )
      FetchContent_MakeAvailable(tinyxml2)
      set(TINYXML2_TARGET tinyxml2::tinyxml2)
    endif()
  endif()
endif()

# ---------- libsolv: pkg-config или ручной поиск ----------
find_package(PkgConfig QUIET)
set(LIBSOLV_TARGET "")
if (PkgConfig_FOUND)
  pkg_check_modules(LIBSOLV QUIET IMPORTED_TARGET libsolv libsolvext)
  if (TARGET PkgConfig::LIBSOLV)
    set(LIBSOLV_TARGET PkgConfig::LIBSOLV)
  endif()
endif()

if (NOT LIBSOLV_TARGET)
  # ручной поиск, если нет pkg-config или модулей
  find_library(LIBSOLV_LIBRARY    NAMES solv)
  find_library(LIBSOLVEXT_LIBRARY NAMES solvext)
  find_path(LIBSOLV_INCLUDE_DIR   solv/pool.h)
  if (LIBSOLV_LIBRARY AND LIBSOLVEXT_LIBRARY AND LIBSOLV_INCLUDE_DIR)
    add_library(LIBSOLV_IMPORTED INTERFACE)
    target_include_directories(LIBSOLV_IMPORTED INTERFACE "${LIBSOLV_INCLUDE_DIR}")
    target_link_libraries(LIBSOLV_IMPORTED INTERFACE "${LIBSOLV_LIBRARY}" "${LIBSOLVEXT_LIBRARY}")
    set(LIBSOLV_TARGET LIBSOLV_IMPORTED)
    message(STATUS "Using system libsolv (manual): ${LIBSOLV_LIBRARY}; ${LIBSOLVEXT_LIBRARY}")
  else()
    message(FATAL_ERROR "libsolv not found. Установи dev-пакеты libsolv/libsolvext или укажи пути вручную.")
  endif()
endif()

# ---------- таргет проекта ----------
add_executable(rpm_graph_fast
  src/main.cpp
  src/exec.cpp
  src/thread_pool.cpp
  src/cache.cpp
  src/repoquery.cpp      # если оставляешь старый backend
  src/graph.cpp
  src/json_writer.cpp
  src/solv_backend.cpp   # libsolv backend
)
target_include_directories(rpm_graph_fast PRIVATE include)

target_link_libraries(rpm_graph_fast
  PRIVATE
    spdlog::spdlog_header_only
    fmt::fmt
    ${TINYXML2_TARGET}
    ${LIBSOLV_TARGET}
)

# При желании приглушаем логи в Release
if (CMAKE_BUILD_TYPE STREQUAL "Release")
  target_compile_definitions(rpm_graph_fast PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()
