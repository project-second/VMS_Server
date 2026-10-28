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
add_subdirectory(lib/app-media-server)

find_package(yaml-cpp CONFIG REQUIRED)
# find_package(OpenCV QUIET)

add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/app
    ${CMAKE_SOURCE_DIR}/include/vms-api
    ${CMAKE_SOURCE_DIR}/include/onvif-manager
    ${CMAKE_SOURCE_DIR}/include/media
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
    app-media-server
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
)

# DT_RUNPATH는 직접 dependency에만 적용되어 libgstrtspserver -> libgstrtsp 같은
# 전이 dependency가 bundled lib directory를 찾지 못한다. DT_RPATH를 사용해
# 패키지 내부 lib 경로가 전체 dependency chain에 적용되게 한다.
if(UNIX AND NOT APPLE)
    target_link_options(${PROJECT_NAME} PRIVATE "LINKER:--disable-new-dtags")
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

# RtspRelayServer는 gstreamer-rtsp-server의 public API를 직접 사용한다.
# GET_RUNTIME_DEPENDENCIES가 정적 library의 새 참조를 놓치거나 이전 build cache를
# 재사용하더라도 배포본에서 RTSP server/core runtime이 빠지지 않도록 명시한다.
find_library(VMS_GSTREAMER_RTSP_SERVER_LIBRARY
    NAMES gstrtspserver-1.0
    REQUIRED
)
find_library(VMS_GSTREAMER_RTSP_LIBRARY
    NAMES gstrtsp-1.0
    REQUIRED
)

install(CODE "
    file(INSTALL
        DESTINATION \"\${CMAKE_INSTALL_PREFIX}/lib\"
        TYPE SHARED_LIBRARY
        FOLLOW_SYMLINK_CHAIN
        FILES
            \"${VMS_GSTREAMER_RTSP_SERVER_LIBRARY}\"
            \"${VMS_GSTREAMER_RTSP_LIBRARY}\"
    )
")

install(CODE [[
    file(GET_RUNTIME_DEPENDENCIES
        EXECUTABLES "$<TARGET_FILE:vms_server>"
        RESOLVED_DEPENDENCIES_VAR resolved_deps
        UNRESOLVED_DEPENDENCIES_VAR unresolved_deps
        POST_EXCLUDE_REGEXES
            "/lib.*/ld-linux.*"
            "/lib.*/libc\\.so.*"
            "/lib.*/libm\\.so.*"
            "/lib.*/libpthread\\.so.*"
            "/lib.*/librt\\.so.*"
            "/lib.*/libdl\\.so.*"
    )

    foreach(dep IN LISTS resolved_deps)
        file(INSTALL
            DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
            TYPE SHARED_LIBRARY
            FOLLOW_SYMLINK_CHAIN
            FILES "${dep}")
    endforeach()

    if(unresolved_deps)
        message(WARNING "Unresolved runtime dependencies: ${unresolved_deps}")
    endif()
]])

install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources
    DESTINATION .
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/db
    DESTINATION .
)

set(VMS_GSTREAMER_PLUGIN_DIR /usr/lib/x86_64-linux-gnu/gstreamer-1.0 CACHE PATH "GStreamer plugin directory to package")
set(VMS_GSTREAMER_PLUGIN_SCANNER /usr/lib/x86_64-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner CACHE FILEPATH "GStreamer plugin scanner to package")

set(VMS_GSTREAMER_PLUGIN_FILES
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstrtsp.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstrtp.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstrtpmanager.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstudp.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgsttcp.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstvideoparsersbad.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstmultifile.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstlibav.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstvideoconvertscale.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstcoreelements.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstapp.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstisomp4.so
    ${VMS_GSTREAMER_PLUGIN_DIR}/libgstmpegtsmux.so
)

foreach(plugin_file IN LISTS VMS_GSTREAMER_PLUGIN_FILES)
    if(EXISTS "${plugin_file}")
        install(FILES "${plugin_file}" DESTINATION lib/gstreamer-1.0)
    else()
        message(WARNING "GStreamer plugin not found and will not be packaged: ${plugin_file}")
    endif()
endforeach()

if(EXISTS "${VMS_GSTREAMER_PLUGIN_SCANNER}")
    install(PROGRAMS "${VMS_GSTREAMER_PLUGIN_SCANNER}" DESTINATION libexec/gstreamer-1.0)
else()
    message(WARNING "GStreamer plugin scanner not found and will not be packaged: ${VMS_GSTREAMER_PLUGIN_SCANNER}")
endif()

install(CODE [[
    set(gst_plugin_files
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstrtsp.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstrtp.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstrtpmanager.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstudp.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgsttcp.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstvideoparsersbad.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstmultifile.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstlibav.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstvideoconvertscale.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstcoreelements.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstapp.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstisomp4.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstmpegtsmux.so"
        "/usr/lib/x86_64-linux-gnu/gstreamer1.0/gstreamer-1.0/gst-plugin-scanner"
    )

    set(existing_gst_plugin_files)
    foreach(plugin_file IN LISTS gst_plugin_files)
        if(EXISTS "${plugin_file}")
            list(APPEND existing_gst_plugin_files "${plugin_file}")
        endif()
    endforeach()

    if(existing_gst_plugin_files)
        file(GET_RUNTIME_DEPENDENCIES
            LIBRARIES ${existing_gst_plugin_files}
            RESOLVED_DEPENDENCIES_VAR gst_resolved_deps
            UNRESOLVED_DEPENDENCIES_VAR gst_unresolved_deps
            POST_EXCLUDE_REGEXES
                "/lib.*/ld-linux.*"
                "/lib.*/libc\\.so.*"
                "/lib.*/libm\\.so.*"
                "/lib.*/libpthread\\.so.*"
                "/lib.*/librt\\.so.*"
                "/lib.*/libdl\\.so.*"
        )

        foreach(dep IN LISTS gst_resolved_deps)
            file(INSTALL
                DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
                TYPE SHARED_LIBRARY
                FOLLOW_SYMLINK_CHAIN
                FILES "${dep}")
        endforeach()

        if(gst_unresolved_deps)
            message(WARNING "Unresolved GStreamer runtime dependencies: ${gst_unresolved_deps}")
        endif()
    endif()
]])

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "Linear CCTV")
set(CPACK_PACKAGE_CONTACT "Linear CCTV")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "VMS Server")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "TGZ")

include(CPack)
