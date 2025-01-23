cmake_minimum_required(VERSION 3.20)
project(libtest-user)

find_package(kivakirjasto)

add_executable(libtest-user main.c)

target_link_libraries(libtest-user PRIVATE kivakirjasto)
