cmake_minimum_required(VERSION 3.16)

project(onvif-gsoap LANGUAGES C CXX)

set(ONVIF_GSOAP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(ONVIF_GSOAP_WSDL_DIR ${ONVIF_GSOAP_ROOT}/wsdl)
set(ONVIF_GSOAP_GENERATED_DIR ${ONVIF_GSOAP_ROOT}/generated)
set(ONVIF_GSOAP_HEADER ${ONVIF_GSOAP_GENERATED_DIR}/onvif.h)

find_program(WSDL2H_EXECUTABLE wsdl2h)
find_program(SOAPCPP2_EXECUTABLE soapcpp2)
find_package(PkgConfig QUIET)

set(ONVIF_GSOAP_IMPORT_DIR /usr/share/gsoap/import CACHE PATH "gSOAP import directory")
set(ONVIF_GSOAP_TYPEMAP /usr/share/gsoap/WS/typemap.dat CACHE FILEPATH "gSOAP typemap file")

file(GLOB ONVIF_WSDL_FILES CONFIGURE_DEPENDS
    ${ONVIF_GSOAP_WSDL_DIR}/*.wsdl
    ${ONVIF_GSOAP_WSDL_DIR}/*.xsd
)

if(NOT WSDL2H_EXECUTABLE OR NOT SOAPCPP2_EXECUTABLE)
    add_custom_target(onvif-gsoap-generate
        COMMAND ${CMAKE_COMMAND} -E echo "gSOAP tools were not found. Install wsdl2h and soapcpp2 before generating ONVIF sources."
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM
    )
    set_target_properties(onvif-gsoap-generate PROPERTIES EXCLUDE_FROM_ALL TRUE)
    message(WARNING "gSOAP tools were not found. Install wsdl2h and soapcpp2 before generating ONVIF sources.")
elseif(NOT ONVIF_WSDL_FILES)
    add_custom_target(onvif-gsoap-generate
        COMMAND ${CMAKE_COMMAND} -E echo "No ONVIF WSDL/XSD files found in ${ONVIF_GSOAP_WSDL_DIR}. Put WSDL/XSD files there first."
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM
    )
    set_target_properties(onvif-gsoap-generate PROPERTIES EXCLUDE_FROM_ALL TRUE)
    message(STATUS "No ONVIF WSDL/XSD files found in ${ONVIF_GSOAP_WSDL_DIR}; skipping generated-source target.")
else()
    add_custom_target(onvif-gsoap-generate
        COMMAND ${CMAKE_COMMAND} -E make_directory ${ONVIF_GSOAP_GENERATED_DIR}
        COMMAND ${WSDL2H_EXECUTABLE}
            -c++17
            -x
            -o ${ONVIF_GSOAP_HEADER}
            -I ${ONVIF_GSOAP_WSDL_DIR}
            -I ${ONVIF_GSOAP_IMPORT_DIR}
            -t ${ONVIF_GSOAP_TYPEMAP}
            ${ONVIF_WSDL_FILES}
        COMMAND ${SOAPCPP2_EXECUTABLE}
            -c++17
            -C
            -x
            -w
            -d ${ONVIF_GSOAP_GENERATED_DIR}
            -I ${ONVIF_GSOAP_IMPORT_DIR}:${ONVIF_GSOAP_GENERATED_DIR}
            ${ONVIF_GSOAP_HEADER}
        WORKING_DIRECTORY ${ONVIF_GSOAP_ROOT}
        COMMENT "Generating ONVIF gSOAP client sources"
        VERBATIM
    )
endif()

file(GLOB ONVIF_GSOAP_SOURCES CONFIGURE_DEPENDS
    ${ONVIF_GSOAP_GENERATED_DIR}/*.cpp
    ${ONVIF_GSOAP_GENERATED_DIR}/*.c
)

if(ONVIF_GSOAP_SOURCES)
    add_library(onvif-gsoap STATIC ${ONVIF_GSOAP_SOURCES})

    target_include_directories(onvif-gsoap PUBLIC
        ${ONVIF_GSOAP_GENERATED_DIR}
        ${ONVIF_GSOAP_IMPORT_DIR}
    )

    if(PkgConfig_FOUND)
        pkg_check_modules(GSOAPSSLPP QUIET gsoapssl++)
    endif()

    if(GSOAPSSLPP_FOUND)
        target_include_directories(onvif-gsoap PUBLIC ${GSOAPSSLPP_INCLUDE_DIRS})
        target_link_libraries(onvif-gsoap PUBLIC ${GSOAPSSLPP_LIBRARIES})
        target_link_directories(onvif-gsoap PUBLIC ${GSOAPSSLPP_LIBRARY_DIRS})
        target_compile_options(onvif-gsoap PUBLIC ${GSOAPSSLPP_CFLAGS_OTHER})
    else()
        find_package(OpenSSL REQUIRED)
        target_link_libraries(onvif-gsoap PUBLIC gsoapssl++ OpenSSL::SSL OpenSSL::Crypto)
    endif()
else()
    add_library(onvif-gsoap INTERFACE)
    target_include_directories(onvif-gsoap INTERFACE ${ONVIF_GSOAP_GENERATED_DIR})
endif()
