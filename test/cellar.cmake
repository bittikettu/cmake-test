set(FILE_PATH ${CMAKE_CURRENT_LIST_FILE})
get_filename_component(FILE_BASENAME ${FILE_PATH} NAME_WE)
set(TEST_APP_POSTFIX ${FILE_BASENAME})
include(lib.cmake)
include(unittest.cmake)