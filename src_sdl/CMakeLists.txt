cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(APP_NAME ${PROJECT_NAME}-sdl3-app)
set(BUILD_SHARED_LIBS OFF)
set(SDL_SHARED OFF)
set(SDL_STATIC ON)
SET(SDL_DISABLE_ANALYZE_MACROS ON)
set(SDL3IMAGE_AVIF OFF)

include(FetchContent)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG origin/main
    GIT_SHALLOW On
    OVERRIDE_FIND_PACKAGE
)

FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
    SDL3_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG origin/main
    GIT_SHALLOW On
    OVERRIDE_FIND_PACKAGE
)

set(SDL3IMAGE_INSTALL OFF)
set(SDL3IMAGE_BUILD_SHARED_LIBS OFF)

FetchContent_MakeAvailable(SDL3_image)

#FetchContent_Declare(
#    harfbuzz
#    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
#    GIT_TAG origin/main
#    OVERRIDE_FIND_PACKAGE
#)

#FetchContent_MakeAvailable(harfbuzz)
#
#FetchContent_Declare(
#    freetype
#    GIT_REPOSITORY https://github.com/freetype/freetype.git
#    GIT_TAG origin/master
#    OVERRIDE_FIND_PACKAGE
#)
#
#FetchContent_MakeAvailable(freetype)
#
#FetchContent_Declare(
#    SDL3_ttf
#    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
#    GIT_TAG origin/main
#    OVERRIDE_FIND_PACKAGE
#)
#
#FetchContent_MakeAvailable(SDL3_ttf)

set(CMAKE_C_CLANG_TIDY "clang-tidy;-checks=clang-diagnostic-*,-clang-diagnostic-unused-parameter")
file(GLOB_RECURSE  SOURCE_FILES "${CMAKE_SOURCE_DIR}/lib/*.c")
set(SRC_FILES main.c ${SOURCE_FILES})
include_directories(${CMAKE_SOURCE_DIR}/lib/)
include_directories(${CMAKE_BINARY_DIR})
add_executable(${APP_NAME} ${SRC_FILES})

target_link_libraries(${APP_NAME} PUBLIC SDL3-static SDL3_image-static freetype SDL3_ttf-static)
