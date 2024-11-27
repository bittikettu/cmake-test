cmake_minimum_required(VERSION 3.20)

# Set the project name
project(HelloWorld)

set(EXECUTABLE_NAME_GCOV "hello_gcov")
set(EXECUTABLE_NAME_TEST "hello_gcov_test")

add_executable(${EXECUTABLE_NAME_TEST} src/main.c src/funktiot.c unity/src/unity.c)

add_executable(${EXECUTABLE_NAME_GCOV} src/main.c src/funktiot.c unity/src/unity.c)
target_compile_options(${EXECUTABLE_NAME_GCOV} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
target_link_libraries(${EXECUTABLE_NAME_GCOV} PRIVATE -fprofile-instr-generate -fcoverage-mapping)

# Add a custom target to format the code
add_custom_target(format
    COMMAND clang-format -i src/*.c
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-format"
)

add_custom_target(profile
    COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${EXECUTABLE_NAME_GCOV}.profraw ./${EXECUTABLE_NAME_GCOV} && llvm-profdata merge -sparse ${EXECUTABLE_NAME_GCOV}.profraw -o ${EXECUTABLE_NAME_GCOV}.profdata && llvm-cov report ./${EXECUTABLE_NAME_GCOV} -instr-profile=${EXECUTABLE_NAME_GCOV}.profdata --ignore-filename-regex="unity/src/*"
    DEPENDS hello_gcov
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
