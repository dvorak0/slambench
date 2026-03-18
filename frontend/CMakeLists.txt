cmake_minimum_required(VERSION 3.16)
project(frontend_benchmarks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_executable(frontend_harris_lk frontend_harris_lk.cpp)
target_link_libraries(frontend_harris_lk PRIVATE ${OpenCV_LIBS})

add_executable(frontend_orb_bf frontend_orb_bf.cpp)
target_link_libraries(frontend_orb_bf PRIVATE ${OpenCV_LIBS})

add_executable(frontend_harris_c frontend_harris_c.cpp)
target_link_libraries(frontend_harris_c PRIVATE ${OpenCV_LIBS} Threads::Threads)

# Halide is only needed for frontend_halide_harris_lk.
# Prefer CMake package discovery (works with official pip wheel / binary installs),
# then fall back to manual header/library lookup.
find_package(Halide CONFIG QUIET)

if(Halide_FOUND)
  message(STATUS "Found Halide via CMake package")
  add_executable(frontend_halide_harris_lk frontend_halide_harris_lk.cpp)
  target_link_libraries(frontend_halide_harris_lk PRIVATE ${OpenCV_LIBS} Halide::Halide)
else()
  find_path(HALIDE_INCLUDE_DIR
    NAMES Halide.h
    HINTS /usr/local/include /usr/include
    PATH_SUFFIXES halide13 halide Halide)
  find_library(HALIDE_LIBRARY
    NAMES Halide libHalide.so
    HINTS /usr/local/lib /usr/lib /usr/lib/x86_64-linux-gnu)

  if(HALIDE_INCLUDE_DIR AND HALIDE_LIBRARY)
    message(STATUS "Found Halide: ${HALIDE_INCLUDE_DIR}, ${HALIDE_LIBRARY}")
    add_executable(frontend_halide_harris_lk frontend_halide_harris_lk.cpp)
    target_include_directories(frontend_halide_harris_lk PRIVATE ${HALIDE_INCLUDE_DIR})
    target_link_libraries(frontend_halide_harris_lk PRIVATE ${OpenCV_LIBS} ${HALIDE_LIBRARY} dl pthread)
  else()
    message(WARNING "Halide not found, frontend_halide_harris_lk will not be built")
  endif()
endif()

# Harris AOT test (links against generator-produced static library)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/harris_manual.h AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/harris_manual.a)
  message(STATUS "Found Harris AOT library, adding frontend_halide_harris_lk")
  add_executable(frontend_halide_harris_lk frontend_halide_harris_lk.cpp)
  target_include_directories(frontend_halide_harris_lk PRIVATE 
    ${HALIDE_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(frontend_halide_harris_lk PRIVATE 
    ${OpenCV_LIBS} 
    ${CMAKE_CURRENT_SOURCE_DIR}/harris_manual.a 
    dl pthread
  )
endif()
