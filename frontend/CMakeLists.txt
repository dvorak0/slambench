cmake_minimum_required(VERSION 3.16)
project(frontend_benchmarks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_executable(frontend_harris_lk frontend_harris_lk.cpp)
target_link_libraries(frontend_harris_lk PRIVATE ${OpenCV_LIBS})

add_executable(frontend_orb_bf frontend_orb_bf.cpp)
target_link_libraries(frontend_orb_bf PRIVATE ${OpenCV_LIBS})

find_path(HALIDE_INCLUDE_DIR
  NAMES Halide.h
  HINTS /usr/include /usr/local/include
  PATH_SUFFIXES halide13 halide Halide)
find_library(HALIDE_LIBRARY
  NAMES Halide Halide13 Halide13-0 Halide14
  HINTS /usr/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu /usr/local/lib)

if(NOT HALIDE_INCLUDE_DIR OR NOT HALIDE_LIBRARY)
  message(FATAL_ERROR "Halide not found. HALIDE_INCLUDE_DIR='${HALIDE_INCLUDE_DIR}', HALIDE_LIBRARY='${HALIDE_LIBRARY}'")
endif()

add_executable(frontend_halide_harris_lk frontend_halide_harris_lk.cpp)
target_include_directories(frontend_halide_harris_lk PRIVATE ${HALIDE_INCLUDE_DIR})
target_link_libraries(frontend_halide_harris_lk PRIVATE ${OpenCV_LIBS} ${HALIDE_LIBRARY})
