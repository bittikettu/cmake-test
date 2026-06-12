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

# Build raylib for the browser (WebGL/GLES2) when configured via emcmake.
if(EMSCRIPTEN)
  set(PLATFORM Web CACHE STRING "raylib platform" FORCE)
endif()

FetchContent_MakeAvailable(raylib)

#set(CMAKE_C_CLANG_TIDY "clang-tidy;-checks=clang-diagnostic-*,-clang-diagnostic-unused-parameter")
file(GLOB_RECURSE  SOURCE_FILES "${CMAKE_SOURCE_DIR}/lib/*.c")
set(SRC_FILES main.c rooms.c ${SOURCE_FILES})
include_directories(${CMAKE_SOURCE_DIR}/lib/)
include_directories(${CMAKE_BINARY_DIR})
add_executable(${APP_NAME} ${SRC_FILES})

target_link_libraries(${APP_NAME} PRIVATE raylib)

if(EMSCRIPTEN)
  # Emit index.html/.js/.wasm/.data so GitHub Pages can serve it directly.
  # boot.mp3 is packed into the .data bundle via --preload-file, and the
  # default shell is replaced with our minimal full-screen canvas page.
  set_target_properties(${APP_NAME} PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
  target_link_options(${APP_NAME} PRIVATE
    "-sUSE_GLFW=3"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sASYNCIFY"
    "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/boot.mp3@boot.mp3"
    "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/separated_keypresses@separated_keypresses"
    "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")
  set_target_properties(${APP_NAME} PROPERTIES LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shell.html;${CMAKE_CURRENT_SOURCE_DIR}/boot.mp3")
else()
  # Native: stage the boot chime and key-click samples next to the executable.
  add_custom_command(TARGET ${APP_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/boot.mp3"
      "$<TARGET_FILE_DIR:${APP_NAME}>/boot.mp3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/separated_keypresses"
      "$<TARGET_FILE_DIR:${APP_NAME}>/separated_keypresses")
endif()
