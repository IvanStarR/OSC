cmake_minimum_required(VERSION 3.16)
project(rpm_graph_fast LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Подтягиваем fmt (опционально; для header-only spdlog можно не тянуть)
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 10.2.1
)
FetchContent_MakeAvailable(fmt)

# Подтягиваем spdlog
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.13.0
)
# Используем встроенный fmt из spdlog? выбери один из вариантов:
set(SPDLOG_FMT_EXTERNAL ON)   # использовать внешний fmt::fmt (рекомендовано)
# set(SPDLOG_FMT_EXTERNAL OFF)  # использовать встроенный fmt внутри spdlog
FetchContent_MakeAvailable(spdlog)

add_executable(rpm_graph_fast
  src/main.cpp
  src/exec.cpp
  src/thread_pool.cpp
  src/cache.cpp
  src/repoquery.cpp
  src/graph.cpp
  src/json_writer.cpp
)
target_include_directories(rpm_graph_fast PRIVATE include)

# ВАЖНО: для header-only используем специальную цель:
target_link_libraries(rpm_graph_fast PRIVATE spdlog::spdlog_header_only fmt::fmt)
