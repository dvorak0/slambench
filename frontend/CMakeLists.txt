cmake_minimum_required(VERSION 3.16)
project(frontend_benchmarks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

find_path(HALIDE_INCLUDE_DIR Halide.h)
find_library(HALIDE_LIBRARY NAMES Halide Halide14 Halide13)

add_executable(frontend_harris_lk frontend_harris_lk.cpp)
target_link_libraries(frontend_harris_lk PRIVATE ${OpenCV_LIBS})

add_executable(frontend_orb_bf frontend_orb_bf.cpp)
target_link_libraries(frontend_orb_bf PRIVATE ${OpenCV_LIBS})

if(HALIDE_INCLUDE_DIR AND HALIDE_LIBRARY)
  add_executable(frontend_halide_harris_lk frontend_halide_harris_lk.cpp)
  target_include_directories(frontend_halide_harris_lk PRIVATE ${HALIDE_INCLUDE_DIR})
  target_link_libraries(frontend_halide_harris_lk PRIVATE ${OpenCV_LIBS} ${HALIDE_LIBRARY})
endif()
