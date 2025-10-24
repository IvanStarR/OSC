cmake_minimum_required(VERSION 3.16)
project(rpm_graph_fast LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog REQUIRED)

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
target_link_libraries(rpm_graph_fast PRIVATE spdlog::spdlog)
