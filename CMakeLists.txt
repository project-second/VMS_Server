cmake_minimum_required(VERSION 3.16)

project(vms_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SOURCES 
	src/*.cpp
	src/*/*.cpp
)

add_subdirectory(lib/app-util)
add_subdirectory(lib/app-http)
add_subdirectory(lib/app-db)
add_subdirectory(lib/app-security)
add_subdirectory(lib/app-onvif)
add_subdirectory(third_party/onvif)

find_package(yaml-cpp CONFIG REQUIRED)
# find_package(OpenCV QUIET)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/vms-api
    ${CMAKE_SOURCE_DIR}/third_party
    # ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    # ${OpenCV_LIBS}
    yaml-cpp::yaml-cpp
    app-util
    app-http
    app-db
    app-security
    app-onvif
    onvif-gsoap
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources
    DESTINATION .
)

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "Linear CCTV")
set(CPACK_PACKAGE_CONTACT "Linear CCTV")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "VMS Server")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "TGZ")

include(CPack)
