cmake_minimum_required(VERSION 3.10)
project(msckf_c LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

add_library(msckf_linalg linalg.c)

add_executable(msckf_runner msckf.c)
target_link_libraries(msckf_runner PRIVATE msckf_linalg m)
target_compile_definitions(msckf_runner PRIVATE MSCKF_USE_ROW_QR=1)

add_executable(msckf_runner_row msckf.c)
target_link_libraries(msckf_runner_row PRIVATE msckf_linalg m)
target_compile_definitions(msckf_runner_row PRIVATE MSCKF_USE_ROW_QR=1)

add_executable(msckf_runner_col msckf.c)
target_link_libraries(msckf_runner_col PRIVATE msckf_linalg m)
target_compile_definitions(msckf_runner_col PRIVATE MSCKF_USE_COL_QR=1)
