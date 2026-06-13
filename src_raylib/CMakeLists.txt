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

# Lua powers runtime-loadable DLC rooms. The official mirror ships no CMake, so
# we build a static lib from its sources below (works for native clang and the
# emscripten/WASM build alike -- Lua is OS-agnostic ISO C).
FetchContent_Declare(
  lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG v5.4.6
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

# Build raylib for the browser (WebGL/GLES2) when configured via emcmake.
if(EMSCRIPTEN)
  set(PLATFORM Web CACHE STRING "raylib platform" FORCE)
endif()

FetchContent_MakeAvailable(raylib lua)

# Build Lua as a static lib from its sources (all .c live at the repo root).
# Drop lua.c/luac.c -- those are the standalone interpreter/compiler mains, not
# library code. No LUA_USE_* macro => pure ISO C: no dlopen/readline, which is
# both maximally WASM-portable and keeps DLC scripts sandboxed.
file(GLOB LUA_SRC "${lua_SOURCE_DIR}/*.c")
# Drop the non-library sources: lua.c/luac.c (CLI mains), onelua.c (single-file
# amalgamation that re-includes everything), ltests.c (internal test harness).
list(REMOVE_ITEM LUA_SRC
  "${lua_SOURCE_DIR}/lua.c"
  "${lua_SOURCE_DIR}/luac.c"
  "${lua_SOURCE_DIR}/onelua.c"
  "${lua_SOURCE_DIR}/ltests.c")
add_library(lua_static STATIC ${LUA_SRC})
target_include_directories(lua_static PUBLIC "${lua_SOURCE_DIR}")

#set(CMAKE_C_CLANG_TIDY "clang-tidy;-checks=clang-diagnostic-*,-clang-diagnostic-unused-parameter")
file(GLOB_RECURSE  SOURCE_FILES "${CMAKE_SOURCE_DIR}/lib/*.c")
set(SRC_FILES main.c rooms.c lua_rooms.c ${SOURCE_FILES})
include_directories(${CMAKE_SOURCE_DIR}/lib/)
include_directories(${CMAKE_BINARY_DIR})
add_executable(${APP_NAME} ${SRC_FILES})

target_link_libraries(${APP_NAME} PRIVATE raylib lua_static)

if(EMSCRIPTEN)
  # Emit index.html/.js/.wasm/.data so GitHub Pages can serve it directly.
  # boot.mp3/running.mp3 are packed into the .data bundle via --preload-file,
  # and the default shell is replaced with our minimal full-screen canvas page.
  set_target_properties(${APP_NAME} PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
  # NOTE: each --preload-file/--shell-file is wrapped in SHELL: so CMake keeps
  # the flag and its path together and does NOT de-duplicate the repeated
  # --preload-file flag (otherwise the second path is dropped as a stray input).
  target_link_options(${APP_NAME} PRIVATE
    "-sUSE_GLFW=3"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sASYNCIFY"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/boot.mp3@boot.mp3"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/running.mp3@running.mp3"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/freesound_community-closing-metal-door-44280.mp3@freesound_community-closing-metal-door-44280.mp3"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/freesound_community-opening-metal-door-98518.mp3@freesound_community-opening-metal-door-98518.mp3"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/separated_keypresses@separated_keypresses"
    "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/rooms@rooms"
    "SHELL:--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
  set_target_properties(${APP_NAME} PROPERTIES LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shell.html;${CMAKE_CURRENT_SOURCE_DIR}/boot.mp3;${CMAKE_CURRENT_SOURCE_DIR}/running.mp3;${CMAKE_CURRENT_SOURCE_DIR}/freesound_community-closing-metal-door-44280.mp3;${CMAKE_CURRENT_SOURCE_DIR}/freesound_community-opening-metal-door-98518.mp3")
else()
  # Native: stage the boot chime, room hum and key-click samples next to the exe.
  add_custom_command(TARGET ${APP_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/boot.mp3"
      "$<TARGET_FILE_DIR:${APP_NAME}>/boot.mp3"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/running.mp3"
      "$<TARGET_FILE_DIR:${APP_NAME}>/running.mp3"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/freesound_community-closing-metal-door-44280.mp3"
      "$<TARGET_FILE_DIR:${APP_NAME}>/freesound_community-closing-metal-door-44280.mp3"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/freesound_community-opening-metal-door-98518.mp3"
      "$<TARGET_FILE_DIR:${APP_NAME}>/freesound_community-opening-metal-door-98518.mp3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/separated_keypresses"
      "$<TARGET_FILE_DIR:${APP_NAME}>/separated_keypresses"
    # DLC rooms: staged next to the exe so users can drop new .lua files in here.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/rooms"
      "$<TARGET_FILE_DIR:${APP_NAME}>/rooms")
endif()
