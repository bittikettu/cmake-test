cmake_minimum_required(VERSION 3.20)

# Set the project name
project(HelloWorld)

set(EXECUTABLE_NAME_GCOV "hello_gcov")
set(EXECUTABLE_NAME_TEST "hello_test")

execute_process(
    COMMAND git describe --tags --always --dirty
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

string(REGEX MATCH "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)([^\\.]*)$" VERSION_MATCH ${GIT_VERSION})
if(VERSION_MATCH)
    set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
    set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
else()
    set(PROJECT_VERSION_MAJOR 0)
    set(PROJECT_VERSION_MINOR 0)
    set(PROJECT_VERSION_PATCH 0)
endif()

execute_process(
    COMMAND git rev-parse --short HEAD
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Set the project version
set(PROJECT_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")

# Configure the version.h file
configure_file(version.h.in version.h @ONLY)
#target_include_directories(${EXECUTABLE_NAME_TEST} PUBLIC
#                           "${PROJECT_BINARY_DIR}"
#                           )

add_executable(${EXECUTABLE_NAME_TEST} src/main.c src/funktiot.c unity/src/unity.c)
add_executable(${EXECUTABLE_NAME_GCOV} src/main.c src/funktiot.c unity/src/unity.c)
target_compile_options(${EXECUTABLE_NAME_GCOV} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
target_link_libraries(${EXECUTABLE_NAME_GCOV} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
target_include_directories(${EXECUTABLE_NAME_GCOV} PRIVATE
                           "${PROJECT_BINARY_DIR}"
                           )
target_include_directories(${EXECUTABLE_NAME_TEST} PRIVATE
                           "${PROJECT_BINARY_DIR}"
                           )

# Add a custom target to format the code
add_custom_target(format
    COMMAND clang-format -i src/*.c
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-format"
)

add_custom_target(profile
    COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${EXECUTABLE_NAME_GCOV}.profraw ${EXECUTABLE_NAME_GCOV}.exe
    COMMAND llvm-profdata merge -sparse ${EXECUTABLE_NAME_GCOV}.profraw -o ${EXECUTABLE_NAME_GCOV}.profdata
    COMMAND llvm-cov report ${EXECUTABLE_NAME_GCOV}.exe -instr-profile=${EXECUTABLE_NAME_GCOV}.profdata --ignore-filename-regex="unity/src/*"
    DEPENDS hello_gcov
    BYPRODUCTS ${EXECUTABLE_NAME_GCOV}.profdata ${EXECUTABLE_NAME_GCOV}.profraw
    WORKING_DIRECTORY ./
)

add_custom_target(profile_info
    COMMAND llvm-cov show ./hello_gcov -instr-profile=hello_gcov.profdata --ignore-filename-regex="unity/src/*"
    DEPENDS profile
    WORKING_DIRECTORY ./
)

# Enable testing
enable_testing()

# Add a test that runs the hello executable
add_test(NAME test 
COMMAND ./${EXECUTABLE_NAME_TEST}
DEPENDS ${EXECUTABLE_NAME_TEST}
)
