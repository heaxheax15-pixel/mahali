# app::network — Local API server (QHttpServer + SyncProcessor) + sync client protocol.
add_library(mahali-network STATIC)

target_include_directories(mahali-network
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(mahali-network
    PUBLIC
        mahali-core
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::HttpServer)

set_target_properties(mahali-network PROPERTIES
    POSITION_INDEPENDENT_CODE ON)