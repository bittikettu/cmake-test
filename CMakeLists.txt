cmake_minimum_required(VERSION 3.20)
project(modular_cmake)

if(BUILD_PHASE STREQUAL "library")
    message("BUILD_PHASE has the expected value ${BUILD_PHASE}.")
    add_subdirectory(lib)
else()
    message("BUILD_PHASE does not have the expected value.")
    add_subdirectory(src_raylib)
endif()
# Firmware version comes from `git describe` at configure time. Prefer a value
# passed in explicitly (-DGIT_VERSION=...): the Docker/WASM build sets this from
# the host, because the container's git often refuses to `describe` the COPYed,
# root-owned tree and would otherwise silently leave us at 0.0.0. Fall back to
# asking git directly (the normal native build path).
if(NOT GIT_VERSION)
  execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

# Tag form is vMAJOR.MINOR.PATCH with an optional describe suffix (-N-gHASH,
# -dirty, or both). The suffix is optional so a clean checkout sitting exactly
# on a tag still parses instead of dropping to 0.0.0.
if(GIT_VERSION MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(-(.+))?$")
  set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
  set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
  set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
  if(CMAKE_MATCH_5)
    set(PROJECT_VERSION_HASH ${CMAKE_MATCH_5})
  else()
    set(PROJECT_VERSION_HASH "release")
  endif()
else()
  set(PROJECT_VERSION_MAJOR 0)
  set(PROJECT_VERSION_MINOR 0)
  set(PROJECT_VERSION_PATCH 0)
  set(PROJECT_VERSION_HASH "unknown")
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
configure_file(clangd.in ${CMAKE_SOURCE_DIR}/.clangd @ONLY)

enable_testing()
