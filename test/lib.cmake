find_package(kivakirjasto)
set(SRC_FILES ${SOURCE_FILES})
set(TEST_BINARY_NAME ${APP_NAME}-${TEST_APP_POSTFIX})
add_executable(${TEST_BINARY_NAME} ${TEST_APP_POSTFIX}.c ${SRC_FILES})

include_directories(${CMAKE_BINARY_DIR})

target_link_libraries(${TEST_BINARY_NAME} PRIVATE jansson)
target_link_libraries(${TEST_BINARY_NAME} PRIVATE unity)
target_link_libraries(${TEST_BINARY_NAME} PRIVATE kivakirjasto)
