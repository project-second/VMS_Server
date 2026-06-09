cmake_minimum_required(VERSION 3.16)

project(app-media-server LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(GSTREAMER REQUIRED IMPORTED_TARGET
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-video-1.0
    gstreamer-rtp-1.0
    gstreamer-rtsp-1.0
    gstreamer-rtsp-server-1.0
)

add_library(${PROJECT_NAME} STATIC
    MediaLiveStreamService.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PUBLIC
    app-util
    Threads::Threads
    PkgConfig::GSTREAMER
)
