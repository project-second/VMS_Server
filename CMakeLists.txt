cmake_minimum_required(VERSION 3.16)

project(vms_server LANGUAGES CXX)

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

find_package(yaml-cpp CONFIG REQUIRED)
# find_package(OpenCV QUIET)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include/
    # ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    # ${OpenCV_LIBS}
    yaml-cpp::yaml-cpp
    app-util
    app-http
    app-db
    app-security
)