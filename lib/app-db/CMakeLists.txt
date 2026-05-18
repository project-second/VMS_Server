cmake_minimum_required(VERSION 3.16)

project(app-db)

file(GLOB SOURCES
    *.cpp
)

add_library(${PROJECT_NAME} STATIC ${SOURCES})

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQXX REQUIRED IMPORTED_TARGET libpqxx)
find_package(PostgreSQL REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/lib/app-util/include/
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    PkgConfig::LIBPQXX
    PostgreSQL::PostgreSQL
    yaml-cpp::yaml-cpp
    app-util
)
