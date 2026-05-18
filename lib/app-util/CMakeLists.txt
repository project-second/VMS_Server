cmake_minimum_required(VERSION 3.16)

project(app-util)
file(GLOB SOURCES 
    *.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCES})

find_package(Threads REQUIRED)
find_package(pugixml CONFIG REQUIRED)

target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PUBLIC 
    Threads::Threads 
    pugixml::pugixml
)
