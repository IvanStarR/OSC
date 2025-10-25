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
                find_program(CLANG_TIDY clang-tidy-15)

                if(NOT CLANG_TIDY)
                        message(FATAL_ERROR "clang-tidy-15 is not found! Please install it.")
                endif()

                set(CMAKE_CXX_CLANG_TIDY "clang-tidy-15;-format-style=file;-header-filter=^(${CMAKE_CURRENT_SOURCE_DIR}/application|${CMAKE_CURRENT_SOURCE_DIR}/source|${CMAKE_CURRENT_SOURCE_DIR}/include)/;-config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy")
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

if (VERSION_MAJOR STREQUAL "default")
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

option(URINGKV_ENABLE_TESTS "Build tests" ON)

if (URINGKV_ENABLE_TESTS)
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

