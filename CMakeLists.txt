cmake_minimum_required(VERSION 3.13) # CMake version check
project(example)                     # Create project "example"
set(CMAKE_CXX_STANDARD 20)           # Enable C++20 standard

find_package(Dawn REQUIRED)

add_executable(example "example.cpp")

target_link_libraries(example PRIVATE dawn::webgpu_dawn)
