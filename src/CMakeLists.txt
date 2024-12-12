# Add the source files
set(SOURCE_FILES main.c funktiot.c)
add_executable(${PROJECT_NAME} ${SOURCE_FILES})
# Enable clang-tidy for C
set(CMAKE_C_CLANG_TIDY "clang-tidy;-checks=clang-diagnostic-*,-clang-diagnostic-unused-parameter")

# Set the include directories
include_directories(../include)
include_directories(../build)
target_link_libraries(${PROJECT_NAME} PRIVATE unity)

