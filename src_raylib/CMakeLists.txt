cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(APP_NAME ${PROJECT_NAME}-raylib-app)
# Add the source files
include(FetchContent)

#FetchContent_Declare(
#  jansson
#  #GIT_REPOSITORY https://github.com/akheron/jansson.git
#  GIT_REPOSITORY https://github.com/Andrew-Au/jansson.git
#  GIT_TAG cmake_update/revised/merge
#  GIT_SHALLOW On
#)

FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_SHALLOW On
)

#FetchContent_Declare(
#  raygui
#  GIT_REPOSITORY https://github.com/raysan5/raygui.git
#  GIT_SHALLOW On
#)



set(JANSSON_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "" FORCE)
set(JANSSON_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(raylib)

set(CMAKE_C_CLANG_TIDY "clang-tidy;-checks=clang-diagnostic-*,-clang-diagnostic-unused-parameter")
file(GLOB_RECURSE  SOURCE_FILES "${CMAKE_SOURCE_DIR}/lib/*.c")
set(SRC_FILES main.c ${SOURCE_FILES})
include_directories(${CMAKE_SOURCE_DIR}/lib/)
include_directories(${CMAKE_BINARY_DIR})
add_executable(${APP_NAME} ${SRC_FILES})

target_link_libraries(${APP_NAME} PRIVATE raylib)

