# app::data — QtSql + SQLite repositories (Windows central DB).
add_library(mahali-data STATIC)

target_include_directories(mahali-data
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(mahali-data
    PUBLIC
        mahali-core
        Qt${QT_VERSION_MAJOR}::Sql)

target_sources(mahali-data PRIVATE
    database.cpp
    audit_log_repository.cpp
    cash_movement_repository.cpp
    cash_session_repository.cpp
    customer_repository.cpp
    customer_transaction_item_repository.cpp
    customer_transaction_repository.cpp
    device_repository.cpp
    expense_repository.cpp
    owner_drawing_repository.cpp
    payment_repository.cpp
    product_repository.cpp
    sale_item_repository.cpp
    sale_repository.cpp
    setting_repository.cpp
    stock_movement_repository.cpp
    supplier_repository.cpp
    supplier_transaction_repository.cpp
    user_repository.cpp
    zakat_setting_repository.cpp)

set_target_properties(mahali-data PROPERTIES
    POSITION_INDEPENDENT_CODE ON)