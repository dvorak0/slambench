# Minimal CHOLMOD package config for SuiteSparse < 7.0 systems (e.g. Ubuntu
# 22.04 ships suitesparse 5.10, which has no upstream *Config.cmake files).
#
# g2o's solvers/cholmod requires the imported target SuiteSparse::CHOLMOD, so
# provide it here and point CHOLMOD_DIR at the directory holding this file.

find_path(CHOLMOD_INCLUDE_DIR
  NAMES cholmod.h
  PATH_SUFFIXES suitesparse)

find_library(CHOLMOD_LIBRARY NAMES cholmod)

if(NOT CHOLMOD_INCLUDE_DIR OR NOT CHOLMOD_LIBRARY)
  set(CHOLMOD_FOUND FALSE)
  return()
endif()

if(NOT TARGET SuiteSparse::CHOLMOD)
  add_library(SuiteSparse::CHOLMOD UNKNOWN IMPORTED)
  set_target_properties(SuiteSparse::CHOLMOD PROPERTIES
    IMPORTED_LOCATION "${CHOLMOD_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${CHOLMOD_INCLUDE_DIR}")
endif()

set(CHOLMOD_VERSION "system")
set(CHOLMOD_FOUND TRUE)
