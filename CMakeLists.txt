cmake_minimum_required(VERSION 3.20)
project(modular_cmake)


#add_subdirectory(src_raylib)
#add_subdirectory(src_sdl)
#add_subdirectory(src_clay)
add_subdirectory(test)


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
#MESSAGE(${CMAKE_BUILD_TYPE})
MESSAGE(${PROJECT_VERSION})
# Configure the version.h file
configure_file(version.h.in version.h @ONLY)

enable_testing()
