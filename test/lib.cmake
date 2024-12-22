file(GLOB_RECURSE  SOURCE_FILES "${CMAKE_SOURCE_DIR}/lib/*.c")
set(SRC_FILES ${SOURCE_FILES})
set(TEST_BINARY_NAME ${APP_NAME}-${TEST_APP_POSTFIX})
add_executable(${TEST_BINARY_NAME} ${TEST_APP_POSTFIX}.c ${SRC_FILES})

include_directories(../include)
include_directories(${CMAKE_SOURCE_DIR}/lib)
include_directories(${CMAKE_BINARY_DIR})

target_link_libraries(${TEST_BINARY_NAME} PRIVATE jansson)
target_link_libraries(${TEST_BINARY_NAME} PRIVATE unity)