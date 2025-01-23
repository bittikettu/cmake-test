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

enable_testing()
