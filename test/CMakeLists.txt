cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(APP_NAME ${PROJECT_NAME}-test)
# Add the source files
include(FetchContent)
FetchContent_Declare(
  unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_SHALLOW On
)
FetchContent_Declare(
  jansson
  #GIT_REPOSITORY https://github.com/akheron/jansson.git
  GIT_REPOSITORY https://github.com/Andrew-Au/jansson.git
  GIT_TAG cmake_update/revised/merge 
  GIT_SHALLOW On
)

set(JANSSON_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(JANSSON_WITHOUT_TESTS ON CACHE BOOL "" FORCE)
set(JANSSON_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(unity jansson)

include(calc.cmake)
include(cellar.cmake)
