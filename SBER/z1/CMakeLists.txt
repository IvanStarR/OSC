cmake_minimum_required(VERSION 3.10)
project(rpm_graph_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(rpm_graph
    src/main.cpp
)

find_package(Threads REQUIRED)
target_link_libraries(rpm_graph PRIVATE Threads::Threads)
