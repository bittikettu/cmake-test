# generate_git_hashes.cmake
string(REPLACE ";" " " SOURCE_FILES_STRING "${SOURCE_FILES}")
message(${SOURCE_FILES_STRING})
separate_arguments(SOURCE_FILES_LIST UNIX_COMMAND "${SOURCE_FILES_STRING}")

#message(STATUS "WITTU ${FOLDER}")
##message(STATUS "WITTU ${CMAKE_SOURCE_DIR}")
#message(STATUS ${SOURCE_FILES_LIST})

file(WRITE ${CMAKE_BINARY_DIR}/git_hashes.h
     "// Generated git hashes for source files\n\n")
# foreach(SOURCE_FILE ${SOURCE_FILES_LIST}) message(STATUS "WITTU
# ${CMAKE_SOURCE_DIR} ${SOURCE_FILE}") endforeach()

foreach(SOURCE_FILE ${SOURCE_FILES_LIST})
  # MESSAGE("WITTU")
  get_filename_component(FILE_NAME ${SOURCE_FILE} NAME)
  #message(
  #  "git log -1 --format=%h ${FOLDER}/${SOURCE_FILE}"
  #)
  execute_process(
    COMMAND git log -1 --format=%h ${FOLDER}/${SOURCE_FILE}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  string(REPLACE "." "_" FILE "${FILE_NAME}")
  file(APPEND ${CMAKE_BINARY_DIR}/git_hashes.h
       "#define ${FILE}_GIT_HASH \"${GIT_HASH}\"\n")
endforeach()

file(APPEND ${CMAKE_BINARY_DIR}/git_hashes.h
      "\nenum versions {\n")

foreach(SOURCE_FILE ${SOURCE_FILES_LIST})
  get_filename_component(FILE_NAME ${SOURCE_FILE} NAME)

  string(REPLACE "." "_" FILE "${FILE_NAME}")
  file(APPEND ${CMAKE_BINARY_DIR}/git_hashes.h
       "\t${FILE}_index,\n")
endforeach()

file(APPEND ${CMAKE_BINARY_DIR}/git_hashes.h
      "\tmax_ver_index\n};\n\n")

file(APPEND ${CMAKE_BINARY_DIR}/git_hashes.h
"struct tversion {
\tenum versions index;
\tchar *hash;
};\n\n")

file(APPEND ${CMAKE_BINARY_DIR}/git_hashes.h
"struct tversion verinfo[max_ver_index] = {\n")

foreach(SOURCE_FILE ${SOURCE_FILES_LIST})
  get_filename_component(FILE_NAME ${SOURCE_FILE} NAME)

  string(REPLACE "." "_" FILE "${FILE_NAME}")
  file(APPEND ${CMAKE_BINARY_DIR}/git_hashes.h
       "\t{.index=${FILE}_index,.hash=${FILE}_GIT_HASH},\n")
endforeach()

file(APPEND ${CMAKE_BINARY_DIR}/git_hashes.h
"};\n")
