cmake_minimum_required(VERSION 3.10)
project(msckf_c LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

add_library(msckf_linalg linalg.c)

add_executable(msckf_runner msckf.c)
target_link_libraries(msckf_runner PRIVATE msckf_linalg m)
