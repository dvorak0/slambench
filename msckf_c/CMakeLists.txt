cmake_minimum_required(VERSION 3.10)
project(msckf_c LANGUAGES C)

add_library(msckf_linalg linalg.c)

add_executable(msckf_runner msckf.c)
target_link_libraries(msckf_runner PRIVATE msckf_linalg m)
