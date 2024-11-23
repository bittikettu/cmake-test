cmake_minimum_required(VERSION 3.10)

# Set the project name
project(HelloWorld)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang)

# Set the C compiler to Clang

# Add an executable
add_executable(hello src/main.c unity/src/unity.c)


# Add a custom target to format the code
add_custom_target(format
    COMMAND clang-format -i src/*.c
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-format"
)

# Enable testing
enable_testing()

# Add a test that runs the hello executable
add_test(NAME RunHello COMMAND hello.exe)
