cmake_minimum_required(VERSION 3.16)

project(app-onvif LANGUAGES C CXX)

set(ONVIF_WSDD_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/onvif_wsdd.h)
set(ONVIF_WSDD_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(ONVIF_GSOAP_IMPORT_DIR /usr/share/gsoap/import CACHE PATH "gSOAP import directory")
set(ONVIF_GSOAP_PLUGIN_DIR /usr/share/gsoap/plugin CACHE PATH "gSOAP plugin directory")

find_program(SOAPCPP2_EXECUTABLE soapcpp2 REQUIRED)

set(ONVIF_WSDD_GENERATED_SOURCES
    ${ONVIF_WSDD_GENERATED_DIR}/soapC.cpp
    ${ONVIF_WSDD_GENERATED_DIR}/soapClient.cpp
)

set(ONVIF_WSDD_GENERATED_HEADERS
    ${ONVIF_WSDD_GENERATED_DIR}/soapH.h
    ${ONVIF_WSDD_GENERATED_DIR}/soapStub.h
    ${ONVIF_WSDD_GENERATED_DIR}/wsdd.nsmap
)

add_custom_command(
    OUTPUT ${ONVIF_WSDD_GENERATED_SOURCES} ${ONVIF_WSDD_GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ONVIF_WSDD_GENERATED_DIR}
    COMMAND ${SOAPCPP2_EXECUTABLE}
        -c++17
        -C
        -x
        -w
        -d ${ONVIF_WSDD_GENERATED_DIR}
        -I ${ONVIF_GSOAP_IMPORT_DIR}
        ${ONVIF_WSDD_INPUT}
    DEPENDS ${ONVIF_WSDD_INPUT}
    COMMENT "Generating ONVIF WS-Discovery gSOAP sources"
    VERBATIM
)

add_library(${PROJECT_NAME} STATIC
    OnvifDiscoveryService.cpp
    ${ONVIF_WSDD_GENERATED_SOURCES}
    ${ONVIF_GSOAP_PLUGIN_DIR}/wsddapi.c
    ${ONVIF_GSOAP_PLUGIN_DIR}/wsaapi.c
)

set_source_files_properties(
    ${ONVIF_GSOAP_PLUGIN_DIR}/wsddapi.c
    ${ONVIF_GSOAP_PLUGIN_DIR}/wsaapi.c
    PROPERTIES LANGUAGE CXX
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${ONVIF_WSDD_GENERATED_DIR}
    ${ONVIF_GSOAP_IMPORT_DIR}
    ${ONVIF_GSOAP_PLUGIN_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC app-util gsoap++)

target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_NONAMESPACES)
