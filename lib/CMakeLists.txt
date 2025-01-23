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
    calc/calc.h
    calc/include/funktiot.h
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
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${INCLUDE_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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