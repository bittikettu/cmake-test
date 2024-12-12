# generate_git_hashes.cmake
string(REPLACE ";" " " SOURCE_FILES_STRING "${SOURCE_FILES}")
separate_arguments(SOURCE_FILES_LIST UNIX_COMMAND "${SOURCE_FILES_STRING}")

message(STATUS "WITTU ${FOLDER}")

file(WRITE ${CMAKE_BINARY_DIR}/git_hashes.h
     "// Generated git hashes for source files\n\n")
# foreach(SOURCE_FILE ${SOURCE_FILES_LIST}) message(STATUS "WITTU
# ${CMAKE_SOURCE_DIR} ${SOURCE_FILE}") endforeach()

foreach(SOURCE_FILE ${SOURCE_FILES_LIST})
  # MESSAGE("WITTU")
  get_filename_component(FILE_NAME ${SOURCE_FILE} NAME)
  execute_process(
    COMMAND git log -1 --format=%h ../${SOURCE_FILE}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REPLACE "." "_" FILE "${FILE_NAME}")
  file(APPEND ${CMAKE_BINARY_DIR}/git_hashes.h
       "#define ${FILE}_GIT_HASH \"${GIT_HASH}\"\n")
endforeach()
