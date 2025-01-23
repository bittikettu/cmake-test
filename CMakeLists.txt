cmake_minimum_required(VERSION 3.20)
project(modular_cmake)

if(BUILD_PHASE STREQUAL "library")
    message("BUILD_PHASE has the expected value ${BUILD_PHASE}.")
    add_subdirectory(lib)
else()
    message("BUILD_PHASE does not have the expected value.")
    add_subdirectory(libtest-user)
    add_subdirectory(test)
endif()
execute_process(
  COMMAND git describe --tags --always --dirty
  OUTPUT_VARIABLE GIT_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE)

string(REGEX MATCH "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)-([^\\.]*)$" VERSION_MATCH ${GIT_VERSION})
if(VERSION_MATCH)
  set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
  set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
  set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
  set(PROJECT_VERSION_HASH ${CMAKE_MATCH_4})
else()
  set(PROJECT_VERSION_MAJOR 0)
  set(PROJECT_VERSION_MINOR 0)
  set(PROJECT_VERSION_PATCH 0)
  set(PROJECT_VERSION_HASH 0)
endif()

execute_process(
  COMMAND git rev-parse --short HEAD
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# Set the project version
set(PROJECT_VERSION
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)
enable_testing()
