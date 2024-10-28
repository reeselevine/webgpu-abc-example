# Include webgpu directory, to define the 'webgpu' target

cmake_minimum_required(VERSION 3.10)
project(Try)
include(FetchContent)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

FetchContent_Declare(
  webgpu
  GIT_REPOSITORY git@github.com:reeselevine/WebGPU-distribution.git
  GIT_TAG        dawn
)
FetchContent_MakeAvailable(webgpu)

# Add the stress executable
add_executable(vec_add
  vec_add.cpp)

target_link_libraries(vec_add PRIVATE webgpu)
