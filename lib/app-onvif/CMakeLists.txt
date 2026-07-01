cmake_minimum_required(VERSION 3.16)

project(app-onvif LANGUAGES C CXX)

set(ONVIF_GSOAP_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/onvif_services.h)
set(ONVIF_GSOAP_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(ONVIF_SERVICES_WSDL_HEADER ${ONVIF_GSOAP_GENERATED_DIR}/onvif_services_wsdl.h)
set(ONVIF_DEVICE_WSDL ${CMAKE_CURRENT_SOURCE_DIR}/wsdl/ver10/device/wsdl/devicemgmt.wsdl)
set(ONVIF_MEDIA_WSDL ${CMAKE_CURRENT_SOURCE_DIR}/wsdl/ver10/media/wsdl/media.wsdl)
set(ONVIF_PTZ_WSDL ${CMAKE_CURRENT_SOURCE_DIR}/wsdl/ver20/ptz/wsdl/ptz.wsdl)
set(ONVIF_GSOAP_IMPORT_DIR /usr/share/gsoap/import CACHE PATH "gSOAP import directory")
set(ONVIF_GSOAP_PLUGIN_DIR /usr/share/gsoap/plugin CACHE PATH "gSOAP plugin directory")
set(ONVIF_GSOAP_CUSTOM_DIR /usr/share/gsoap/custom CACHE PATH "gSOAP custom source directory")
set(ONVIF_GSOAP_TYPEMAP /usr/share/gsoap/WS/typemap.dat CACHE FILEPATH "gSOAP typemap file")

find_package(OpenSSL REQUIRED)
find_program(WSDL2H_EXECUTABLE wsdl2h REQUIRED)
find_program(SOAPCPP2_EXECUTABLE soapcpp2 REQUIRED)

file(GLOB_RECURSE ONVIF_WSDL_DEPENDENCIES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/wsdl/*.wsdl
    ${CMAKE_CURRENT_SOURCE_DIR}/wsdl/*.xsd
    ${CMAKE_CURRENT_SOURCE_DIR}/external/*.xsd
)

set(ONVIF_GSOAP_GENERATED_SOURCES
    ${ONVIF_GSOAP_GENERATED_DIR}/soapC.cpp
    ${ONVIF_GSOAP_GENERATED_DIR}/soapClient.cpp
)

set(ONVIF_GSOAP_GENERATED_HEADERS
    ${ONVIF_SERVICES_WSDL_HEADER}
    ${ONVIF_GSOAP_GENERATED_DIR}/soapH.h
    ${ONVIF_GSOAP_GENERATED_DIR}/soapStub.h
    ${ONVIF_GSOAP_GENERATED_DIR}/wsdd.nsmap
    ${ONVIF_GSOAP_GENERATED_DIR}/DeviceBinding.nsmap
    ${ONVIF_GSOAP_GENERATED_DIR}/MediaBinding.nsmap
    ${ONVIF_GSOAP_GENERATED_DIR}/PTZBinding.nsmap
)

add_custom_command(
    OUTPUT ${ONVIF_GSOAP_GENERATED_SOURCES} ${ONVIF_GSOAP_GENERATED_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ONVIF_GSOAP_GENERATED_DIR}
    COMMAND ${WSDL2H_EXECUTABLE}
        -c++17
        -x
        -o ${ONVIF_SERVICES_WSDL_HEADER}
        -I ${ONVIF_GSOAP_IMPORT_DIR}
        -I ${CMAKE_CURRENT_SOURCE_DIR}/external
        -I ${CMAKE_CURRENT_SOURCE_DIR}/wsdl/ver10/schema
        -t ${ONVIF_GSOAP_TYPEMAP}
        ${ONVIF_DEVICE_WSDL}
        ${ONVIF_MEDIA_WSDL}
        ${ONVIF_PTZ_WSDL}
    COMMAND ${SOAPCPP2_EXECUTABLE}
        -c++17
        -C
        -x
        -w
        -d ${ONVIF_GSOAP_GENERATED_DIR}
        -I ${ONVIF_GSOAP_IMPORT_DIR}:${ONVIF_GSOAP_GENERATED_DIR}
        ${ONVIF_GSOAP_INPUT}
    DEPENDS ${ONVIF_GSOAP_INPUT} ${ONVIF_WSDL_DEPENDENCIES}
    COMMENT "Generating ONVIF gSOAP WS-Discovery, Device, Media, and PTZ sources"
    VERBATIM
)

add_library(${PROJECT_NAME} STATIC
    OnvifDiscoveryService.cpp
    OnvifDiscoveryCompat.cpp
    OnvifDeviceClient.cpp
    OnvifMediaClient.cpp
    OnvifPtzClient.cpp
    OnvifSoapContext.cpp
    ${ONVIF_GSOAP_GENERATED_SOURCES}
    ${ONVIF_GSOAP_PLUGIN_DIR}/wsddapi.c
    ${ONVIF_GSOAP_PLUGIN_DIR}/wsaapi.c
    ${ONVIF_GSOAP_PLUGIN_DIR}/wsseapi.c
    ${ONVIF_GSOAP_PLUGIN_DIR}/smdevp.c
    ${ONVIF_GSOAP_PLUGIN_DIR}/mecevp.c
    ${ONVIF_GSOAP_CUSTOM_DIR}/struct_timeval.c
)

set_source_files_properties(
    ${ONVIF_GSOAP_PLUGIN_DIR}/wsddapi.c
    ${ONVIF_GSOAP_PLUGIN_DIR}/wsaapi.c
    ${ONVIF_GSOAP_PLUGIN_DIR}/wsseapi.c
    ${ONVIF_GSOAP_PLUGIN_DIR}/smdevp.c
    ${ONVIF_GSOAP_PLUGIN_DIR}/mecevp.c
    ${ONVIF_GSOAP_CUSTOM_DIR}/struct_timeval.c
    PROPERTIES LANGUAGE CXX
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${ONVIF_GSOAP_GENERATED_DIR}
    ${ONVIF_GSOAP_IMPORT_DIR}
    ${ONVIF_GSOAP_PLUGIN_DIR}
    ${ONVIF_GSOAP_CUSTOM_DIR}
)

target_link_libraries(${PROJECT_NAME} PUBLIC app-util gsoapssl++ OpenSSL::SSL OpenSSL::Crypto)

target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_NONAMESPACES WITH_OPENSSL WITH_DOM)
