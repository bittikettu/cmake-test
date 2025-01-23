cmake_minimum_required(VERSION 3.20)
project(kivakirjastolib VERSION 0.0.1
    DESCRIPTION "A Simple Hello World Application"
    LANGUAGES C
)

set(THIS_LIB_NAME kivakirjasto)
include(GNUInstallDirs)

# Generate version header
execute_process(
  COMMAND git describe --tags --always --dirty
  OUTPUT_VARIABLE GIT_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE)

string(REGEX MATCH "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)-([^\\.]*)$" VERSION_MATCH ${GIT_VERSION})
if(VERSION_MATCH)
  set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
  set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
  set(PROJECT_VERSION_PATCH ${CMAKE_MATCH_3})
  set(PROJECT_VERSION_HASH ${CMAKE_MATCH_4})
else()
  set(PROJECT_VERSION_MAJOR 0)
  set(PROJECT_VERSION_MINOR 0)
  set(PROJECT_VERSION_PATCH 0)
  set(PROJECT_VERSION_HASH 0)
endif()

execute_process(
  COMMAND git rev-parse --short HEAD
  OUTPUT_VARIABLE GIT_COMMIT_HASH
  OUTPUT_STRIP_TRAILING_WHITESPACE)

# Set the project version
set(PROJECT_VERSION
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}"
)
#MESSAGE(${CMAKE_BUILD_TYPE})
MESSAGE(${PROJECT_VERSION})
# Configure the version.h file
configure_file(version_kivakirjasto.h.in version_kivakirjasto.h @ONLY)

set(INCLUDE_HEADERS
    cellar/include/bicycle.h
    cellar/include/bicyclecellar.h
    cellar/cellar.h
    calc/calc.h
    calc/include/funktiot.h
    ${CMAKE_CURRENT_BINARY_DIR}/version_kivakirjasto.h
)

add_library(${THIS_LIB_NAME} STATIC
    cellar/src/bicycle.c 
    cellar/src/bicyclecellar.c
    calc/src/funktiot.c
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
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${THIS_LIB_NAME}
)

install(FILES ${INCLUDE_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${THIS_LIB_NAME})

install(EXPORT ${THIS_LIB_NAME}Targets
    FILE ${THIS_LIB_NAME}Targets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${THIS_LIB_NAME}
)

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

add_custom_target(kivakirjasto_install
    COMMENT "Install the ${THIS_LIB_NAME} to ${CMAKE_INSTALL_PREFIX}"
    COMMAND ${CMAKE_COMMAND} --build . --target install
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS ${THIS_LIB_NAME}
)



set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME ${THIS_LIB_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Juha Viitanen")
set(CPACK_PACKAGE_DESCRIPTION "My Application")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")
set(CPACK_PACKAGE_CONTACT "viidakkovekara@gmail.com")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "MyApp-${CPACK_PACKAGE_VERSION}")
include(CPack)

add_custom_target(pakets
    COMMAND ${CMAKE_COMMAND} --build . --target package
    COMMENT "Generating package using CPack"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)