# app::core — Pure business logic. Zero Qt GUI dependency.
add_library(mahali-core STATIC)

target_include_directories(mahali-core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(mahali-core
    PUBLIC
        Qt${QT_VERSION_MAJOR}::Core
        # UpdateChecker talks to the GitHub releases API. Network is not a GUI
        # dependency, so the "no Qt GUI" rule above still holds.
        Qt${QT_VERSION_MAJOR}::Network)

# The version this binary was built with. PUBLIC so everything linking
# mahali-core (including the tests) compiles against the same string, and so
# currentVersion() can never disagree with what the app reports.
target_compile_definitions(mahali-core
    PUBLIC
        MAHALI_VERSION="${PROJECT_VERSION}")

target_sources(mahali-core PRIVATE
    applied_op.h
    audit_log_entry.h
    cash_movement.h
    cash_session.h
    cash_session_calculator.cpp
    cash_session_calculator.h
    customer.h
    customer_transaction.h
    format_utils.h
    customer_transaction_item.h
    device.h
    expense.h
    owner_drawing.h
    payment.h
    product.h
    profit_loss_calculator.cpp
    profit_loss_calculator.h
    purchase.h
    purchase_item.h
    sale.h
    sale_item.h
    setting.h
    session.cpp
    session.h
    stock_movement.h
    supplier.h
    supplier_payment.h
    supplier_return.h
    supplier_return_item.h
    occasion.h
    sync_operation.h
    sync_operation_codec.cpp
    sync_operation_codec.h
    sync_outbox_entry.h
    update_checker.cpp
    update_checker.h
    update_downloader.cpp
    update_downloader.h
    update_installer.cpp
    update_installer.h
    user.h
    version.cpp
    version.h
    zakat_calculator.cpp
    zakat_calculator.h
    zakat_setting.h)

set_target_properties(mahali-core PROPERTIES
    POSITION_INDEPENDENT_CODE ON)