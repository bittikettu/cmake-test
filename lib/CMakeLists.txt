cmake_minimum_required(VERSION 3.20)
project(kivakirjastolib VERSION 0.0.1
    DESCRIPTION "A Simple Hello World Application"
    LANGUAGES C
)

set(THIS_LIB_NAME kivakirjasto)

include(GNUInstallDirs)

set(INCLUDE_HEADERS
    cellar/include/bicycle.h
    cellar/include/bicyclecellar.h
    cellar/cellar.h
)

add_library(${THIS_LIB_NAME} 
    cellar/src/bicycle.c 
    cellar/src/bicyclecellar.c
    ${INCLUDE_HEADERS}    
)


target_include_directories(${THIS_LIB_NAME} 
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    INTERFACE $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

install(TARGETS ${THIS_LIB_NAME}
    EXPORT ${THIS_LIB_NAME}Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${INCLUDE_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ${THIS_LIB_NAME}Targets
    FILE ${THIS_LIB_NAME}Targets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${THIS_LIB_NAME}
)

#set_target_properties(${THIS_LIB_NAME} PROPERTIES
#    PUBLIC_HEADER "include/bicycle.h"
#    PUBLIC_HEADER "include/bicyclecellar.h"
#)

include(GenerateExportHeader)
generate_export_header(${THIS_LIB_NAME})

include(CMakePackageConfigHelpers)

configure_package_config_file (${CMAKE_CURRENT_SOURCE_DIR}/${THIS_LIB_NAME}.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/${THIS_LIB_NAME}Config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${THIS_LIB_NAME}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${THIS_LIB_NAME}Config.cmake
    DESTINATION DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${THIS_LIB_NAME}
)

#write_basic_package_version_file(
#    ${CMAKE_CURRENT_BINARY_DIR}/${THIS_LIB_NAME}Version.cmake
#    VERSION ${PROJECT_VERSION}
#    COMPATIBILITY SameMajorVersion
#    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${THIS_LIB_NAME}
#)

#export(EXPORT ${THIS_LIB_NAME} FILE ${CMAKE_CURRENT_BINARY_DIR}/${THIS_LIB_NAME}.cmake)