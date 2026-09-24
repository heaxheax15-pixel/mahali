# app::network — Local API server (QHttpServer + SyncProcessor) + sync client protocol.
add_library(mahali-network STATIC)

target_include_directories(mahali-network
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(mahali-network
    PUBLIC
        mahali-core
        mahali-data
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::HttpServer)

target_sources(mahali-network PRIVATE
    sync_client.cpp
    sync_client.h
    sync_protocol.cpp
    sync_protocol.h
    sync_server.cpp
    sync_server.h
    sync_processor.cpp
    sync_processor.h)

set_target_properties(mahali-network PROPERTIES
    POSITION_INDEPENDENT_CODE ON)