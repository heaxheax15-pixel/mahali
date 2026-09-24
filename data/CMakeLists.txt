# app::data — QtSql + SQLite repositories (Windows central DB).
add_library(mahali-data STATIC)

target_include_directories(mahali-data
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(mahali-data
    PUBLIC
        mahali-core
        Qt${QT_VERSION_MAJOR}::Sql)

set_target_properties(mahali-data PROPERTIES
    POSITION_INDEPENDENT_CODE ON)