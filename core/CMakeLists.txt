# app::core — Pure business logic. Zero Qt GUI dependency.
add_library(mahali-core STATIC)

target_include_directories(mahali-core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(mahali-core
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core)

target_sources(mahali-core PRIVATE
    applied_op.h
    audit_log_entry.h
    cash_movement.h
    cash_session.h
    cash_session_calculator.cpp
    cash_session_calculator.h
    customer.h
    customer_transaction.h
    customer_transaction_item.h
    device.h
    expense.h
    owner_drawing.h
    payment.h
    product.h
    profit_loss_calculator.cpp
    profit_loss_calculator.h
    sale.h
    sale_item.h
    setting.h
    session.cpp
    session.h
    stock_movement.h
    supplier.h
    supplier_transaction.h
    sync_operation.h
    sync_operation_codec.cpp
    sync_operation_codec.h
    sync_outbox_entry.h
    user.h
    zakat_calculator.cpp
    zakat_calculator.h
    zakat_setting.h)

set_target_properties(mahali-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON)