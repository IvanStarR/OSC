cmake_minimum_required(VERSION 3.16)

project(uringkv)

include(cmake/module.cmake)
enable_testing()


set(APP_NAME "uringkv" CACHE STRING "App name")


set(BUILD_TYPE ${CMAKE_BUILD_TYPE})

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

string(REPLACE "-" "" DEVICE_CLASS "${APP_NAME}")
string(TOLOWER "${DEVICE_CLASS}" DEVICE_CLASS)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(PERFOMANCE_TRACE "Performance tracing report" OFF)
option(CLANG_TIDY_CHECK "clang-tidy source code check" ON)

if(PLATFORM_ARM32)
        set(CMAKE_EXE_LINKER_FLAGS "-static")
        set(CMAKE_CXX_FLAGS_RELEASE "-Os -ffat-lto-objects -flto=auto -std=c++2a -Wall -Wextra -Werror -Wno-deprecated-declarations -Wno-array-bounds -Wno-unused-result")
else()
        set(CMAKE_CXX_FLAGS_DEBUG "-g3 -DDEBUG -flto=auto -fno-omit-frame-pointer -std=c++2a -ggdb3 -O0 -Wall -Wextra -Werror -Wno-unused-result -Wno-deprecated-declarations -fno-inline")
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto=auto -std=c++2a -Wall -Wextra -Werror -Wno-deprecated-declarations -Wno-unused-result")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g1 -flto=auto -std=c++2a -Wall -Wextra -Werror -Wno-deprecated-declarations -Wno-unused-result")
endif()

if(PERFOMANCE_TRACE)
        set(BUILD_TYPE "${BUILD_TYPE} with perfomance tracing")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DPERFORMANCE_TRACE")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DPERFORMANCE_TRACE")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DPERFORMANCE_TRACE")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT PERFOMANCE_TRACE)
        if(CLANG_TIDY_CHECK)
                #find_program(CLANG_TIDY clang-tidy-15)

                # if(NOT CLANG_TIDY)
                #         message(FATAL_ERROR "clang-tidy-15 is not found! Please install it.")
                #endif()

                # set(CMAKE_CXX_CLANG_TIDY "clang-tidy-15;-format-style=file;-header-filter=^(${CMAKE_CURRENT_SOURCE_DIR}/application|${CMAKE_CURRENT_SOURCE_DIR}/source|${CMAKE_CURRENT_SOURCE_DIR}/include)/;-config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
        endif()
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
file(STRINGS "/etc/os-release" OS_RELEASE_CONTENT REGEX "VERSION_ID")
string(REGEX MATCH "[0-9]+\.[0-9]+" OS_VERSION_ID ${OS_RELEASE_CONTENT})
file(STRINGS "/etc/os-release" OS_RELEASE_CONTENT REGEX "PRETTY_NAME")
string(REGEX MATCH "\".+\"" OS_PRETTY_NAME ${OS_RELEASE_CONTENT})

message(STATUS "BUILD_TYPE = ${BUILD_TYPE}")
message(STATUS "OS_PRETTY_NAME = ${OS_PRETTY_NAME}")
message(STATUS "CMAKE_CXX_FLAGS = ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_CLANG_TIDY = ${CMAKE_CXX_CLANG_TIDY}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG = ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_EXE_LINKER_FLAGS = ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE = ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO = ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

execute_process(COMMAND git submodule update --init --recursive
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE SUBMODULE_UPDATE
        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(COMMAND git rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE REVISION
        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE COMMIT
        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(COMMAND date "+%Y-%m-%d %H:%M:%S"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE TIMESTAMP
        ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(VERSION_MAJOR STREQUAL "default")
        execute_process(COMMAND date "+%Y-%m-%d"
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE VERSION_MAJOR
                ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE
        )
endif()

message(STATUS "Building on [${REVISION}: ${COMMIT}] at ${TIMESTAMP}")


include_directories(include
        ${CMAKE_CURRENT_SOURCE_DIR}
)


include(FetchContent)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)


set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.13.0
)
FetchContent_MakeAvailable(spdlog)


FetchContent_Declare(
  xxhash
  GIT_REPOSITORY https://github.com/Cyan4973/xxHash.git
  GIT_TAG v0.8.2
)
FetchContent_MakeAvailable(xxhash)

# --- выбрать корректный таргет для xxhash или собрать фолбэк ---
if (TARGET xxhash::xxhash)
  set(XXHASH_TARGET xxhash::xxhash)
elseif (TARGET xxhash)
  set(XXHASH_TARGET xxhash)
else()
  message(STATUS "xxhash CMake target not provided by upstream; building fallback static library")

  # Определяем, где реально лежит исходник
  set(XXHASH_SRC "")
  set(XXHASH_INC "")
  if (EXISTS "${xxhash_SOURCE_DIR}/lib/xxhash.c")
    set(XXHASH_SRC "${xxhash_SOURCE_DIR}/lib/xxhash.c")
    set(XXHASH_INC "${xxhash_SOURCE_DIR}/lib")
  elseif (EXISTS "${xxhash_SOURCE_DIR}/xxhash.c")
    set(XXHASH_SRC "${xxhash_SOURCE_DIR}/xxhash.c")
    set(XXHASH_INC "${xxhash_SOURCE_DIR}")
  else()
    message(FATAL_ERROR "Couldn't locate xxhash.c in ${xxhash_SOURCE_DIR} (tried lib/xxhash.c and xxhash.c)")
  endif()

  add_library(xxhash_external STATIC "${XXHASH_SRC}")
  target_include_directories(xxhash_external PUBLIC "${XXHASH_INC}")
  # не применяем clang-tidy/Warnings-as-errors к внешнему коду
  set_property(TARGET xxhash_external PROPERTY CXX_CLANG_TIDY "")
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(xxhash_external PRIVATE -Wno-error)
  endif()
  set(XXHASH_TARGET xxhash_external)
endif()


# 1) Стабильный тег; можно поменять на liburing-2.4
set(URING_GIT_TAG "liburing-2.5" CACHE STRING "liburing tag")
FetchContent_Declare(
  liburing
  GIT_REPOSITORY https://github.com/axboe/liburing.git
  GIT_TAG        ${URING_GIT_TAG}
)

FetchContent_GetProperties(liburing)
if (NOT liburing_POPULATED)
  FetchContent_Populate(liburing)

  # 2) Если у апстрима есть CMakeLists.txt — подключаем его
  if (EXISTS "${liburing_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory(${liburing_SOURCE_DIR} ${liburing_BINARY_DIR}/liburing EXCLUDE_FROM_ALL)
  else()
    # 3) Фолбэк: собираем библиотеку вручную
    file(GLOB LIBURING_SOURCES
      "${liburing_SOURCE_DIR}/src/queue.c"
      "${liburing_SOURCE_DIR}/src/register.c"
      "${liburing_SOURCE_DIR}/src/setup.c"
      "${liburing_SOURCE_DIR}/src/syscall.c"
      "${liburing_SOURCE_DIR}/src/version.c"
      "${liburing_SOURCE_DIR}/src/compat.c"
    )
    if (LIBURING_SOURCES STREQUAL "")
      message(FATAL_ERROR "liburing fetched, but sources not found under src/*.c")
    endif()

    add_library(liburing_external STATIC ${LIBURING_SOURCES})

    # 4) Создадим shim compat.h, если его нет в исходниках
    set(LIBURING_SHIM_DIR "${CMAKE_BINARY_DIR}/liburing_shim/include")
    set(LIBURING_SHIM_FILE "${LIBURING_SHIM_DIR}/liburing/compat.h")
    if (NOT EXISTS "${liburing_SOURCE_DIR}/src/include/liburing/compat.h")
      file(MAKE_DIRECTORY "${LIBURING_SHIM_DIR}/liburing")
      file(WRITE "${LIBURING_SHIM_FILE}"
"// minimal compat shim for manual liburing build
#pragma once
#include <unistd.h>
#include <sys/types.h>
#ifndef __must_check
# define __must_check
#endif
#ifndef LIBURING_INTERNAL
# define LIBURING_INTERNAL
#endif
")
    endif()

    # 5) Порядок include ВАЖЕН: shim -> src/include -> include (на всякий случай) -> src
    target_include_directories(liburing_external PUBLIC
      "${LIBURING_SHIM_DIR}"
      "${liburing_SOURCE_DIR}/src/include"
      "${liburing_SOURCE_DIR}/include"
      "${liburing_SOURCE_DIR}/src"
    )
    target_compile_definitions(liburing_external PRIVATE _GNU_SOURCE)
    set_target_properties(liburing_external PROPERTIES POSITION_INDEPENDENT_CODE ON)
  endif()
endif()

# 6) Выберем имя таргета
set(URING_TARGET "")
if (TARGET uring)
  set(URING_TARGET uring)
elseif (TARGET liburing)
  set(URING_TARGET liburing)
elseif (TARGET liburing_external)
  set(URING_TARGET liburing_external)
endif()

if (URING_TARGET)
  message(STATUS "Fetched liburing target: ${URING_TARGET}")
else()
  message(FATAL_ERROR "liburing fetched, but no library target was created")
endif()


# Определим имя таргета, с которым будем линковаться
set(URING_TARGET "")
if (TARGET uring)
  set(URING_TARGET uring)
elseif (TARGET liburing)
  set(URING_TARGET liburing)
elseif (TARGET liburing_external)
  set(URING_TARGET liburing_external)
endif()

if (URING_TARGET)
  message(STATUS "Fetched liburing target: ${URING_TARGET}")
else()
  message(FATAL_ERROR "liburing fetched, but no library target was created")
endif()

option(URINGKV_ENABLE_TESTS "Build tests" ON)

if(URINGKV_ENABLE_TESTS)
        FetchContent_Declare(
                catch2
                GIT_REPOSITORY https://github.com/catchorg/Catch2.git
                GIT_TAG v3.5.3
        )
        FetchContent_MakeAvailable(catch2)
endif()


add_subdirectory(source)
add_subdirectory(application)
add_subdirectory(tests)

