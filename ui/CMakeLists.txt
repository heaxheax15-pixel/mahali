# app::ui — QtWidgets UI (desktop / Windows central PC).
add_library(mahali-ui STATIC)

target_include_directories(mahali-ui
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(mahali-ui
    PUBLIC
        mahali-core
        mahali-data
        mahali-network
        Qt${QT_VERSION_MAJOR}::Widgets)

target_sources(mahali-ui PRIVATE
    format_utils.h
    main_window.cpp
    main_window.h
    server_controller.cpp
    server_controller.h
    products_page.cpp
    products_page.h
    customers_page.cpp
    customers_page.h
    suppliers_page.cpp
    suppliers_page.h)

set_target_properties(mahali-ui PROPERTIES
    POSITION_INDEPENDENT_CODE ON)