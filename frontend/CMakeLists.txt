cmake_minimum_required(VERSION 3.16)
project(frontend_benchmarks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

# OpenCV frontends
add_executable(frontend_harris_lk frontend_harris_lk.cpp)
target_link_libraries(frontend_harris_lk PRIVATE ${OpenCV_LIBS})

add_executable(frontend_orb_bf frontend_orb_bf.cpp)
target_link_libraries(frontend_orb_bf PRIVATE ${OpenCV_LIBS})

add_executable(frontend_harris_c frontend_harris_c.cpp)
target_link_libraries(frontend_harris_c PRIVATE ${OpenCV_LIBS} Threads::Threads)

# Halide AOT libraries
find_path(HALIDE_INCLUDE_DIR NAMES Halide.h HINTS /usr/local/include /usr/include PATH_SUFFIXES halide13 halide Halide)

# Harris AOT tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/harris_manual.a AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/halide_harris_manual.cpp)
  add_executable(halide_harris_manual halide_harris_manual.cpp)
  target_include_directories(halide_harris_manual PRIVATE ${HALIDE_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(halide_harris_manual PRIVATE ${OpenCV_LIBS} ${CMAKE_CURRENT_SOURCE_DIR}/harris_manual.a dl pthread)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/harris_auto.a AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/halide_harris_auto.cpp)
  add_executable(halide_harris_auto halide_harris_auto.cpp)
  target_include_directories(halide_harris_auto PRIVATE ${HALIDE_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(halide_harris_auto PRIVATE ${OpenCV_LIBS} ${CMAKE_CURRENT_SOURCE_DIR}/harris_auto.a dl pthread)
endif()

# Full frontend test with Halide Harris + Optical Flow
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/harris_auto.a AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/frontend_halide_harris_lk.cpp)
  add_executable(frontend_halide_harris_lk frontend_halide_harris_lk.cpp)
  target_include_directories(frontend_halide_harris_lk PRIVATE ${HALIDE_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(frontend_halide_harris_lk PRIVATE ${OpenCV_LIBS} ${CMAKE_CURRENT_SOURCE_DIR}/harris_auto.a dl pthread)
endif()
