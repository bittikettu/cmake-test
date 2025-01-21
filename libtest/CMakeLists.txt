cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(APP_NAME ${PROJECT_NAME}-libtest)
# Add the source files


set(CMAKE_C_CLANG_TIDY "clang-tidy;-checks=clang-diagnostic-*,-clang-diagnostic-unused-parameter")
set(SRC_FILES main.c)
#include_directories(${CMAKE_SOURCE_DIR}/lib/)
#include_directories(${CMAKE_BINARY_DIR})
add_executable(${APP_NAME} ${SRC_FILES})

#target_link_libraries(${APP_NAME} PRIVATE raylib)

