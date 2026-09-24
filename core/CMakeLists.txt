# app::core — Pure business logic. Zero Qt GUI dependency.
add_library(mahali-core STATIC)

target_include_directories(mahali-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(mahali-core
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core)

set_target_properties(mahali-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON)