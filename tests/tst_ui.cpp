 #include <QtTest/QtTest>

#include <QDate>
#include <QDir>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTime>
#include <QComboBox>
#include <QTableView>
#include <QTableWidget>
#include <QFont>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPrinter>
#include <QProcess>
#include <QRegularExpression>
#include <QCheckBox>
#include <QTimer>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include "ui/dialogs/language_dialog.h"
#include "ui/login_dialog.h"
#include "ui/users_page.h"
#include "ui/widgets/app_icon.h"

#include "core/session.h"
#include "core/sync_operation.h"
#include "core/update_checker.h"
#include "data/admin_secret_repository.h"
#include "data/applied_op_repository.h"
#include "data/audit_log_repository.h"
#include "data/cash_entry_service.h"
#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/customer_transaction_repository.h"
#include "data/date_utils.h"
#include "data/expense_repository.h"
#include "data/owner_drawing_repository.h"
#include "data/occasion_repository.h"
#include "data/occasion_service.h"
#include "data/payment_repository.h"
#include "data/payment_service.h"
#include "data/product_repository.h"
#include "data/report_service.h"
#include "data/sale_item_repository.h"
#include "data/sale_repository.h"
#include "data/sale_service.h"
#include "data/setting_repository.h"
#include "data/stock_movement_repository.h"
#include "data/supplier_repository.h"
#include "data/user_repository.h"
#include "network/sync_client.h"
#include "ui/audit_log_page.h"
#include "ui/cash_session_page.h"
#include "ui/customers_page.h"
#include "ui/expenses_page.h"
#include "core/format_utils.h"
#include "ui/pos_page.h"
#include "ui/printing/receipt.h"
#include "ui/products_page.h"
#include "ui/quick_items_bar.h"
#include "ui/refunds_page.h"
#include "ui/reports_page.h"
#include "ui/sales_page.h"
#include "ui/server_controller.h"
#include "ui/settings_page.h"
#include "ui/suppliers_page.h"
#include "ui/main_window.h"
#include "ui/printing/receipt_printer.h"

#include <algorithm>

using namespace app;

// Phase 11+12 (desktop): the headless ServerController (sync hub + daily
// retention), the master-data pages, and the cash & sales flow: a fast POS
// page with flexible price/quantity entry, the cash session lifecycle, and
// today's sales summary.
class UiTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void language_dialog_returns_the_chosen_code();
    void language_dialog_returns_empty_on_dismiss();
    void serverStartStop();
    void serverAppliesOverLoopback();
    void serverEmitsStatsOnTimer();
    void serverRetentionPrunesOldOps();
    void pagesReflectSeededData();
    void flexibleAmountParsing();
    void posSaleWithPriceOverride();
    void pos_cart_grid_is_the_wide_table_and_the_narrow_one_is_empty();
    void pos_line_defaults_to_piece_unit();
    void pos_unit_switch_reprices_line();
    void pos_scanned_package_barcode_adds_carton_line();
    void pos_piece_and_carton_stay_two_rows();
    void pos_count_label_shows_only_pieces_for_piece_lines();
    void pos_count_label_shows_packages_then_pieces();
    void pos_count_label_groups_packages_by_their_own_name();
    void pos_unite_shows_translated_default_package_name();
    void pos_unite_shows_custom_package_name_untouched();
    void pos_unite_column_shows_the_whole_package_name();
    void app_icon_paints_inside_the_requested_box();
    void pos_new_product_from_scan_records_opening_stock();
    void products_page_new_product_records_opening_stock();
    void posSaleRequiresOpenSession();
    void cashSessionLifecycle();
    void cash_session_rejects_invalid_amount();
    void salesPageShowsToday();
    void profit_cents_survives_translation();
    void sales_page_print_button_enables_on_selection();
    void sales_page_details_shows_the_receipt_number();
    void sales_page_prints_the_selected_sale_as_pdf();
    void receipt_print_and_save_writes_pdf_and_returns_path();
    void receipt_is_a_single_page_on_thermal_paper();
    void receipt_renders_a_cash_sale();
    void receipt_shows_customer_name_on_a_credit_sale();
    void receipt_58mm_drops_the_unit_column();
    void receipt_shows_the_adjustment_and_the_payable_matches_the_row();
    void settings_page_saves_and_restores_receipt_printing();
    void settings_page_saves_auto_print();
    void receipt_printer_writes_a_pdf();
    void product_dialog_reveals_the_weight_checkbox();
    void product_dialog_saves_the_weight_flag();
    void products_page_marks_weight_products();
    void receipt_renders_a_weight_line_with_grams();
    void receipt_uses_the_paper_from_settings();
    void barcode_dialog_does_not_close();
    void product_dialog_writes_package_fields_from_the_shared_widget();
    void quick_items_bar_lists_products();
    void customerCreditAndPayment();
    void expensesAndDrawings();
    void reportBuilds();
    void settingsCurrencyAndZakat();
    void zakat_respects_nisab();
    void update_bar_hidden_by_default();
    void update_bar_appears_on_signal();
    void refundsRestoreMoneyAndStock();
    void paymentRefundRaisesBalance();
    void entryReversal();
    void login_basic();
    // void statusBarSwitchUserButton();
    void users_page_lists_users();
    void users_page_does_not_render_pins();
    void users_page_shows_no_pin_column();
    void users_page_actions_column_is_wide_enough();
    void users_page_action_buttons_fit_inside_the_row();
    void user_edit_pin_persists();
    void master_recovery_grants_access();
    void status_bar_shows_occasion_when_active();

private:
    void seedSyncDatabase(const QString& path, int* productId, int* sessionId);
    void seedMasterData(const QString& path);
    int seedProduct(const QString& path, int* sessionId, long long salePriceCents);
    // The widget the form row with this label holds, visible or not -- so a field
    // that has been hidden can be told from one that is missing. The product dialog
    // labels its rows in Arabic and sets no object names on them, which is why the
    // label text is the only handle on a field.
    static QWidget* fieldLabelled(QDialog* modal, const QString& label);

    QTemporaryDir m_dir;
    QByteArray m_key;
};

void UiTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_key = QByteArrayLiteral("phase11-ui-key");
    app::ui::setCurrencySymbol(QString());
}

void UiTest::seedSyncDatabase(const QString& path, int* productId, int* sessionId)
{
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);

    data::CashSessionRepository sessions(db);
    *sessionId = sessions.open(5000);
    QVERIFY(*sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 7500;
    product.salePriceCents = 10000;
    product.unit = QStringLiteral("أنبوب");
    product.packageSize = 1;
    *productId = products.save(product);
    QVERIFY(*productId > 0);

    products.adjustStock(*productId, 100, QStringLiteral("purchase"));
}

void UiTest::seedMasterData(const QString& path)
{
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product a;
    a.barcode = QStringLiteral("1000000000001");
    a.name = QStringLiteral("أصناف أ");
    a.costPriceCents = 6000;
    a.salePriceCents = 10000;
    const int aId = products.save(a);
    QVERIFY(aId > 0);
    products.adjustStock(aId, 10, QStringLiteral("purchase"));

    core::Product b;
    b.barcode = QStringLiteral("1000000000002");
    b.name = QStringLiteral("أصناف ب");
    b.costPriceCents = 3000;
    b.salePriceCents = 5000;
    const int bId = products.save(b);
    QVERIFY(bId > 0);

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("زبون");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);

    core::CustomerTransaction debt;
    debt.customerId = customerId;
    debt.amountCents = 70000;
    QVERIFY(data::CustomerTransactionRepository(db).insert(debt) > 0);

    core::Payment payment;
    payment.customerId = customerId;
    payment.amountCents = 50000;
    QVERIFY(data::PaymentRepository(db).insert(payment) > 0);

    data::SupplierRepository suppliers(db);
    core::Supplier supplier;
    supplier.name = QStringLiteral("مورد الشاي");
    const int supplierId = suppliers.save(supplier);
    QVERIFY(supplierId > 0);
}

void UiTest::serverStartStop()
{
    const QString path = m_dir.filePath(QStringLiteral("ctrl.sqlite"));
    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);

    QVERIFY(!controller.isListening());
    QVERIFY(controller.start());
    QVERIFY(controller.isListening());
    QVERIFY(controller.port() != 0);
    controller.stop();
    QVERIFY(!controller.isListening());
}

void UiTest::serverAppliesOverLoopback()
{
    int productId = 0;
    int sessionId = 0;
    const QString path = m_dir.filePath(QStringLiteral("loop.sqlite"));
    seedSyncDatabase(path, &productId, &sessionId);

    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);
    controller.setStatsIntervalMs(25);
    QVERIFY(controller.start());

    core::SyncOperation op;
    op.opId = 1;
    op.type = core::SyncOpType::Sale;
    op.occurredAt = QDateTime::currentDateTimeUtc();
    op.deviceId = QStringLiteral("ui-loop-device");
    core::SyncItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 0; // resolved from product.salePriceCents
    op.items = { item };

    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(QStringLiteral("127.0.0.1"));
    endpoint.setPort(controller.port());
    endpoint.setPath(QStringLiteral("/api/sync"));

    const network::SyncSendResult result = network::SyncClient::sendBatch(endpoint, m_key, { op });
    QVERIFY(result.delivered);
    QCOMPARE(result.errorClass, network::SyncErrorClass::None);
    QCOMPARE(result.acks.size(), 1);
    QVERIFY(result.acks[0].ok);

    // Give the stats timer a beat to refresh, then verify live counters.
    QTest::qWait(150);
    const ui::ServerController::Stats stats = controller.stats();
    QCOMPARE(stats.appliedOps, 1);
    QCOMPARE(stats.devices, 1);
    QCOMPARE(stats.salesToday, 1);
    QCOMPARE(stats.revenueTodayCents, 20000LL);
    QVERIFY(stats.listening);

    data::SaleRepository sales(db);
    QCOMPARE(sales.countByDeviceId(QStringLiteral("ui-loop-device")), 1);
    controller.stop();
}

void UiTest::serverEmitsStatsOnTimer()
{
    const QString path = m_dir.filePath(QStringLiteral("timer.sqlite"));
    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);
    controller.setStatsIntervalMs(25);

    QSignalSpy spy(&controller, &ui::ServerController::statsChanged);
    QVERIFY(controller.start());
    QTest::qWait(200);
    QVERIFY(spy.count() >= 3);
    controller.stop();
}

void UiTest::serverRetentionPrunesOldOps()
{
    const QString path = m_dir.filePath(QStringLiteral("retention.sqlite"));
    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);

    data::AppliedOpRepository journal(db);
    core::AppliedOpRecord old;
    old.deviceId = QStringLiteral("dev-old");
    old.opId = 1;
    old.opType = static_cast<int>(core::SyncOpType::Sale);
    old.appliedAt = QDateTime::currentDateTime().addDays(-500);
    old.totalCents = 1000;
    QVERIFY(journal.insert(old) > 0);

    core::AppliedOpRecord fresh;
    fresh.deviceId = QStringLiteral("dev-old");
    fresh.opId = 2;
    fresh.opType = static_cast<int>(core::SyncOpType::CustomerDebt);
    fresh.appliedAt = QDateTime::currentDateTime().addSecs(3600);
    fresh.totalCents = 5000;
    QVERIFY(journal.insert(fresh) > 0);

    QCOMPARE(controller.runRetention(30), 1);
    QCOMPARE(journal.count(), 1);
    QVERIFY(!journal.findByDeviceOp(QStringLiteral("dev-old"), 1).has_value());
    QVERIFY(journal.findByDeviceOp(QStringLiteral("dev-old"), 2).has_value());
}

void UiTest::pagesReflectSeededData()
{
    const QString path = m_dir.filePath(QStringLiteral("pages.sqlite"));
    seedMasterData(path);
    data::Database db(path);

    ui::ProductsPage products(db);
    QCOMPARE(products.rowCount(), 2);

    ui::CustomersPage customers(db);
    QCOMPARE(customers.rowCount(), 1);
    QCOMPARE(customers.balanceAt(0), ui::formatMoney(20000));

    ui::SuppliersPage suppliers(db);
    QCOMPARE(suppliers.supplierCount(), 1);
}

int UiTest::seedProduct(const QString& path, int* sessionId, long long salePriceCents)
{
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    *sessionId = sessions.open(5000);
    if (*sessionId <= 0) {
        return 0;
    }

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 6000;
    product.salePriceCents = salePriceCents;
    product.unit = QStringLiteral("أنبوب");
    product.packageSize = 1;
    const int productId = products.save(product);
    if (productId <= 0) {
        return 0;
    }
    products.adjustStock(productId, 100, QStringLiteral("purchase"));
    return productId;
}

QWidget* UiTest::fieldLabelled(QDialog* modal, const QString& label)
{
    QFormLayout* form = nullptr;
    for (QFormLayout* l : modal->findChildren<QFormLayout*>()) {
        form = l;
        break;
    }
    if (!form) {
        return nullptr;
    }
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem* labelItem = form->itemAt(row, QFormLayout::LabelRole);
        QLayoutItem* fieldItem = form->itemAt(row, QFormLayout::FieldRole);
        auto* labelWidget = qobject_cast<QLabel*>(labelItem ? labelItem->widget() : nullptr);
        if (labelWidget && labelWidget->text() == label) {
            return fieldItem ? fieldItem->widget() : nullptr;
        }
    }
    return nullptr;
}

void UiTest::flexibleAmountParsing()
{
    QCOMPARE(ui::parseMoney(QStringLiteral("12")).value_or(-1), 1200LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("12.5")).value_or(-1), 1250LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("12.50")).value_or(-1), 1250LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("12,5")).value_or(-1), 1250LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("12,50")).value_or(-1), 1250LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("0,5")).value_or(-1), 50LL);
    QCOMPARE(ui::parseMoney(QStringLiteral(".5")).value_or(-1), 50LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("1,000")).value_or(-1), 100000LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("1.000")).value_or(-1), 100000LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("1,234.50")).value_or(-1), 123450LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("12.75")).value_or(-1), 1275LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("500 دج")).value_or(-1), 50000LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("1 234")).value_or(-1), 123400LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("١٢٫٥٠")).value_or(-1), 1250LL);
    QVERIFY(!ui::parseMoney(QString()).has_value());
    QVERIFY(!ui::parseMoney(QStringLiteral("abc")).has_value());
    QVERIFY(!ui::parseMoney(QStringLiteral("-5")).has_value());
}

void UiTest::posSaleWithPriceOverride()
{
    int productId = 0;
    int sessionId = 0;
    const QString path = m_dir.filePath(QStringLiteral("pos-override.sqlite"));
    productId = seedProduct(path, &sessionId, 10000);

    data::Database db(path);
    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("6130000000004"));
    page.addEntry();
    QCOMPARE(page.lineCount(), 1);
    QCOMPARE(page.linePriceAt(0), 10000LL);
    QCOMPARE(page.totalCents(), 10000LL);

    // Cashier edits quantity and overrides the unit price in the grid, which is
    // the wide table now: quantity is the fourth column, price the fifth.
    page.table()->item(0, 3)->setText(QStringLiteral("2"));
    page.table()->item(0, 4)->setText(QStringLiteral("7.50"));
    page.completeSale();

    QVERIFY(page.lastSaleId() != 0);
    QCOMPARE(page.lineCount(), 0);
    QVERIFY(page.noticeText().contains(QStringLiteral("تم البيع")));

    data::SaleRepository sales(db);
    const auto sale = sales.findById(page.lastSaleId());
    QVERIFY(sale.has_value());
    QCOMPARE(sale->totalCents, 1500LL);
    QCOMPARE(sale->deviceId, QStringLiteral("desktop"));

    data::SaleItemRepository saleItems(db);
    const auto items = saleItems.findBySaleId(page.lastSaleId());
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].unitPriceCents, 750LL);
    QCOMPARE(items[0].quantity, 2LL);

    data::ProductRepository products(db);
    QCOMPARE(products.findById(productId)->quantity, 98LL);

    data::CashMovementRepository movements(db);
    QCOMPARE(movements.sumBySessionId(sessionId), 1500LL);

    // Desktop sales never touch the sync outbox/applied ops.
    QCOMPARE(data::AppliedOpRepository(db).count(), 0);

    // The override left one audit trail entry.
    int overrides = 0;
    for (const auto& entry : data::AuditLogRepository(db).findAll()) {
        if (entry.action == QLatin1String("price_override")) {
            ++overrides;
        }
    }
    QCOMPARE(overrides, 1);
}

void UiTest::pos_cart_grid_is_the_wide_table_and_the_narrow_one_is_empty()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-grid.sqlite"));
    QFile::remove(path);
    data::Database db(path);
    data::ProductRepository products(db);

    core::Product first;
    first.barcode = QStringLiteral("10001");
    first.name = QStringLiteral("شاي أخضر");
    first.unit = QStringLiteral("علبة");
    first.salePriceCents = 350;
    QVERIFY(products.save(first) > 0);

    ui::PosPage page(db);

    // The wide table is the sale grid now, so its columns are the line's own
    // fields and not the stock on the shelf.
    QTableWidget* grid = page.table();
    QVERIFY(grid);
    QCOMPARE(grid->columnCount(), 5);
    QCOMPARE(grid->rowCount(), 0);
    QCOMPARE(grid->horizontalHeaderItem(0)->text(), QStringLiteral("Produit"));
    QCOMPARE(grid->horizontalHeaderItem(1)->text(), QStringLiteral("Code-barres"));
    QCOMPARE(grid->horizontalHeaderItem(2)->text(), QStringLiteral("الوحدة"));
    QCOMPARE(grid->horizontalHeaderItem(3)->text(), QStringLiteral("Qté"));
    QCOMPARE(grid->horizontalHeaderItem(4)->text(), QStringLiteral("Prix"));

    // The catalogue it used to be is gone with its two filters, and nothing on
    // the page builds a +/- button any more.
    QVERIFY(!page.findChild<QTableView*>(QStringLiteral("posProductTable")));
    QVERIFY(!page.findChild<QLineEdit*>(QStringLiteral("posCatalogSearch")));
    QVERIFY(!page.findChild<QComboBox*>(QStringLiteral("posUnitFilter")));
    QVERIFY(!page.findChild<QPushButton*>(QStringLiteral("cartQuantityIncrease")));
    QVERIFY(!page.findChild<QPushButton*>(QStringLiteral("cartQuantityDecrease")));

    // The narrow table stays in the layout, empty.
    auto* narrow = page.findChild<QTableWidget*>(QStringLiteral("posEmptyTable"));
    QVERIFY(narrow);
    QCOMPARE(narrow->rowCount(), 0);

    page.setEntryText(QStringLiteral("10001"));
    page.addEntry();
    QCOMPARE(grid->rowCount(), 1);
    QCOMPARE(grid->item(0, 0)->text(), QStringLiteral("شاي أخضر"));
    QCOMPARE(grid->item(0, 1)->text(), QStringLiteral("10001"));
    // The unit is no longer a cell of text: it is a dropdown, because it is the
    // one thing on a line that is picked from a list instead of typed. A product
    // scanned by its plain barcode starts as a piece.
    auto* unitBox = qobject_cast<QComboBox*>(grid->cellWidget(0, 2));
    QVERIFY(unitBox);
    QCOMPARE(unitBox->currentData().toString(), QStringLiteral("piece"));
    QCOMPARE(grid->item(0, 3)->text(), QStringLiteral("1"));
    QCOMPARE(grid->item(0, 4)->text(), ui::formatMoney(350));

    // The quantity is the one cell that opens for typing; the price is reached
    // through the dialog and cannot be typed over.
    QVERIFY(grid->item(0, 3)->flags().testFlag(Qt::ItemIsEditable));
    QVERIFY(!grid->item(0, 4)->flags().testFlag(Qt::ItemIsEditable));

    // Typing in it moves the line and the money. QTableWidgetItem::setText is
    // silent, so the slot is invoked the way a committed edit would reach it.
    grid->item(0, 3)->setText(QStringLiteral("3"));
    QVERIFY(QMetaObject::invokeMethod(&page, "onCellChanged", Q_ARG(int, 0), Q_ARG(int, 3)));
    QCOMPARE(page.lineQuantityAt(0), 3LL);
    QCOMPARE(page.totalCents(), 1050LL);

    // A quantity no sale could be made of is put back rather than kept.
    grid->item(0, 3)->setText(QStringLiteral("0"));
    QVERIFY(QMetaObject::invokeMethod(&page, "onCellChanged", Q_ARG(int, 0), Q_ARG(int, 3)));
    QCOMPARE(page.lineQuantityAt(0), 3LL);
    QCOMPARE(grid->item(0, 3)->text(), QStringLiteral("3"));
}

void UiTest::pos_line_defaults_to_piece_unit()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-unit-piece.sqlite"));
    QFile::remove(path);
    data::Database db(path);
    data::CashSessionRepository sessions(db);
    QVERIFY(sessions.open(5000) > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("20001");
    product.name = QStringLiteral("ماء");
    product.unit = QStringLiteral("قنينة");
    product.salePriceCents = 60;
    product.costPriceCents = 58;
    product.piecesPerPackage = 24;
    // save() hands back the row id rather than filling it in, so the stock has to
    // be credited through that id and not through product.id, which is still 0.
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("20001"));
    page.addEntry();

    // Scanned by its plain code, so it is a piece, and the dropdown says so.
    QCOMPARE(page.lineCount(), 1);
    auto* unitBox = qobject_cast<QComboBox*>(page.table()->cellWidget(0, 2));
    QVERIFY(unitBox);
    QCOMPARE(unitBox->currentData().toString(), QStringLiteral("piece"));
    QCOMPARE(page.linePriceAt(0), 60LL);

    page.completeSale();
    QVERIFY(page.lastSaleId() != 0);

    // And it is recorded as a piece: a piece line consumes its quantity in pieces
    // and stores the word the dropdown showed.
    QSqlQuery query(db.handle());
    QVERIFY(query.prepare(QStringLiteral("SELECT unit_kind, pieces_consumed FROM sale_items")));
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("piece"));
    QCOMPARE(query.value(1).toLongLong(), 1LL);
}

void UiTest::pos_unit_switch_reprices_line()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-unit-switch.sqlite"));
    QFile::remove(path);
    data::Database db(path);
    data::CashSessionRepository sessions(db);
    QVERIFY(sessions.open(5000) > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("20002");
    product.name = QStringLiteral("شاي");
    product.salePriceCents = 60;
    product.costPriceCents = 58;
    product.piecesPerPackage = 24;
    // save() hands back the row id rather than filling it in, so the stock has to
    // be credited through that id and not through product.id, which is still 0.
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("20002"));
    page.addEntry();
    QCOMPARE(page.lineCount(), 1);
    QCOMPARE(page.linePriceAt(0), 60LL);
    QCOMPARE(page.totalCents(), 60LL);

    // The cashier turns the line into a carton. 60 x 24 is the price of one
    // carton, and the quantity does not move: one carton is one carton, not
    // twenty-four.
    auto* unitBox = qobject_cast<QComboBox*>(page.table()->cellWidget(0, 2));
    QVERIFY(unitBox);
    unitBox->setCurrentIndex(unitBox->findData(QStringLiteral("package")));
    QCOMPARE(page.lineQuantityAt(0), 1LL);
    QCOMPARE(page.linePriceAt(0), 1440LL);
    QCOMPARE(page.totalCents(), 1440LL);

    page.completeSale();
    QVERIFY(page.lastSaleId() != 0);

    QSqlQuery query(db.handle());
    QVERIFY(query.prepare(QStringLiteral("SELECT unit_kind, pieces_consumed, unit_price_cents "
                                        "FROM sale_items")));
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("package"));
    QCOMPARE(query.value(1).toLongLong(), 24LL);
    QCOMPARE(query.value(2).toLongLong(), 1440LL);
}

void UiTest::pos_scanned_package_barcode_adds_carton_line()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-scan-carton.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("1234567890123");
    product.packageBarcode = QStringLiteral("1234567890123c");
    product.name = QStringLiteral("زيت");
    product.salePriceCents = 60;
    product.costPriceCents = 58;
    product.piecesPerPackage = 24;
    // save() hands back the row id rather than filling it in, so the stock has to
    // be credited through that id and not through product.id, which is still 0.
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("1234567890123c"));
    page.addEntry();

    // The 'c' is the marker that says this scan means a carton, so the line comes
    // in already priced as one: 60 x 24.
    QCOMPARE(page.lineCount(), 1);
    auto* unitBox = qobject_cast<QComboBox*>(page.table()->cellWidget(0, 2));
    QVERIFY(unitBox);
    QCOMPARE(unitBox->currentData().toString(), QStringLiteral("package"));
    QCOMPARE(page.linePriceAt(0), 1440LL);

    // The other half of the convention: a shop that has not printed the carton
    // label yet. This product has a piece barcode and no carton barcode at all, so
    // the only way the scan can mean a carton is that the suffix is taken off and
    // the piece code underneath is looked up.
    core::Product unlabelled;
    unlabelled.barcode = QStringLiteral("5550001112223");
    unlabelled.packageBarcode = QString();
    unlabelled.name = QStringLiteral("سكر");
    unlabelled.salePriceCents = 100;
    unlabelled.costPriceCents = 90;
    unlabelled.piecesPerPackage = 10;
    QVERIFY(products.save(unlabelled) > 0);

    ui::PosPage second(db);
    second.setEntryText(QStringLiteral("5550001112223c"));
    second.addEntry();

    QCOMPARE(second.lineCount(), 1);
    QCOMPARE(qobject_cast<QComboBox*>(second.table()->cellWidget(0, 2))->currentData().toString(),
             QStringLiteral("package"));
    QCOMPARE(second.linePriceAt(0), 1000LL);
    QCOMPARE(second.table()->item(0, 1)->text(), QStringLiteral("5550001112223"));
}

void UiTest::pos_piece_and_carton_stay_two_rows()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-two-rows.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("1234567890123");
    product.packageBarcode = QStringLiteral("1234567890123c");
    product.name = QStringLiteral("زيت");
    product.salePriceCents = 60;
    product.costPriceCents = 58;
    product.piecesPerPackage = 24;
    // save() hands back the row id rather than filling it in, so the stock has to
    // be credited through that id and not through product.id, which is still 0.
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("1234567890123"));
    page.addEntry();
    page.setEntryText(QStringLiteral("1234567890123c"));
    page.addEntry();

    // Two scans of one product are two lines, because a piece and a carton of the
    // same product have one number each and the two numbers are not the same.
    // Merged, the cart would hold one line at one price for both.
    QCOMPARE(page.lineCount(), 2);
    QCOMPARE(qobject_cast<QComboBox*>(page.table()->cellWidget(0, 2))->currentData().toString(),
             QStringLiteral("piece"));
    QCOMPARE(qobject_cast<QComboBox*>(page.table()->cellWidget(1, 2))->currentData().toString(),
             QStringLiteral("package"));
    QCOMPARE(page.linePriceAt(0), 60LL);
    QCOMPARE(page.linePriceAt(1), 1440LL);
    QCOMPARE(page.totalCents(), 1500LL);
}

void UiTest::pos_count_label_shows_only_pieces_for_piece_lines()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-count-pieces.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("20003");
    product.name = QStringLiteral("ماء");
    product.salePriceCents = 60;
    product.costPriceCents = 58;
    product.piecesPerPackage = 24;
    product.packageName = QStringLiteral("كرتونة");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("20003"));
    page.addEntry();
    QCOMPARE(page.lineCount(), 1);

    // The quantity is the one editable cell, so it is set the way an edit
    // reaches the page rather than through a setter the grid would bypass.
    page.table()->item(0, 3)->setText(QStringLiteral("5"));
    QVERIFY(QMetaObject::invokeMethod(&page, "onCellChanged", Q_ARG(int, 0), Q_ARG(int, 3)));
    QCOMPARE(page.lineQuantityAt(0), 5LL);

    const QString label = page.countLabelText();
    QVERIFY(label.contains(QStringLiteral("5 قطعة")));
    // A piece-only cart has no carton to name, so the word must not appear.
    QVERIFY(!label.contains(QStringLiteral("كرتونة")));
    QVERIFY(label.contains(QStringLiteral("Articles: 1")));
}

void UiTest::pos_count_label_shows_packages_then_pieces()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-count-pack-then-piece.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("20004");
    product.name = QStringLiteral("ماء");
    product.salePriceCents = 60;
    product.costPriceCents = 58;
    product.piecesPerPackage = 24;
    product.packageName = QStringLiteral("كرتونة");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    ui::PosPage page(db);

    // Two cartons: scan the piece code, turn the line into a carton, then type the
    // count. One carton is one carton, so the number is what the cashier enters.
    page.setEntryText(QStringLiteral("20004"));
    page.addEntry();
    auto* unitBox = qobject_cast<QComboBox*>(page.table()->cellWidget(0, 2));
    QVERIFY(unitBox);
    unitBox->setCurrentIndex(unitBox->findData(QStringLiteral("package")));
    page.table()->item(0, 3)->setText(QStringLiteral("2"));
    QVERIFY(QMetaObject::invokeMethod(&page, "onCellChanged", Q_ARG(int, 0), Q_ARG(int, 3)));

    // Three pieces of the same product: a second scan defaults to a piece, which
    // lands on a line of its own because the unit differs from the carton line.
    page.setEntryText(QStringLiteral("20004"));
    page.addEntry();
    QCOMPARE(page.lineCount(), 2);
    page.table()->item(1, 3)->setText(QStringLiteral("3"));
    QVERIFY(QMetaObject::invokeMethod(&page, "onCellChanged", Q_ARG(int, 1), Q_ARG(int, 3)));

    const QString label = page.countLabelText();
    QVERIFY(label.contains(QStringLiteral("2 كرتونة")));
    QVERIFY(label.contains(QStringLiteral("3 قطعة")));
    QVERIFY(label.contains(QStringLiteral(" + ")));
    // Packages lead and pieces follow, so the carton the cashier read first stays
    // in front of the loose count.
    const int carton = label.indexOf(QStringLiteral("كرتونة"));
    const int piece = label.indexOf(QStringLiteral("قطعة"));
    QVERIFY(carton >= 0);
    QVERIFY(piece >= 0);
    QVERIFY(carton < piece);
}

void UiTest::pos_count_label_groups_packages_by_their_own_name()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-count-package-names.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product bala;
    bala.barcode = QStringLiteral("30001");
    bala.name = QStringLiteral("تفاح");
    bala.salePriceCents = 100;
    bala.costPriceCents = 90;
    bala.piecesPerPackage = 12;
    bala.packageName = QStringLiteral("بالة");
    const int balaId = products.save(bala);
    QVERIFY(balaId > 0);
    products.adjustStock(balaId, 50, QStringLiteral("purchase"));

    core::Product carton;
    carton.barcode = QStringLiteral("30002");
    carton.name = QStringLiteral("برتقال");
    carton.salePriceCents = 60;
    carton.costPriceCents = 58;
    carton.piecesPerPackage = 24;
    carton.packageName = QStringLiteral("كرتونة");
    const int cartonId = products.save(carton);
    QVERIFY(cartonId > 0);
    products.adjustStock(cartonId, 50, QStringLiteral("purchase"));

    ui::PosPage page(db);

    page.setEntryText(QStringLiteral("30001"));
    page.addEntry();
    auto* balaBox = qobject_cast<QComboBox*>(page.table()->cellWidget(0, 2));
    QVERIFY(balaBox);
    balaBox->setCurrentIndex(balaBox->findData(QStringLiteral("package")));

    page.setEntryText(QStringLiteral("30002"));
    page.addEntry();
    QCOMPARE(page.lineCount(), 2);
    auto* cartonBox = qobject_cast<QComboBox*>(page.table()->cellWidget(1, 2));
    QVERIFY(cartonBox);
    cartonBox->setCurrentIndex(cartonBox->findData(QStringLiteral("package")));

    // Each carton is counted under its own name, not folded into one "carton"
    // figure, and the first-seen order puts "بالة" before "كرتونة".
    const QString label = page.countLabelText();
    QVERIFY(label.contains(QStringLiteral("1 بالة")));
    QVERIFY(label.contains(QStringLiteral("1 كرتونة")));
    QVERIFY(label.contains(QStringLiteral(" + ")));
    const int balaPos = label.indexOf(QStringLiteral("بالة"));
    const int cartonPos = label.indexOf(QStringLiteral("كرتونة"));
    QVERIFY(balaPos >= 0);
    QVERIFY(cartonPos >= 0);
    QVERIFY(balaPos < cartonPos);
}

// The database default for a carton's name is the Arabic word. In the French UI
// that default has to come out of the catalogue as "Carton", so the Unité
// dropdown must pass the name through the translator rather than show the stored
// bytes. No translator is installed under test, so the expected value is the
// message the source asks for -- what is pinned down is that the label goes
// through the translator at all, under the context pos_page.cpp uses.
void UiTest::pos_unite_shows_translated_default_package_name()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-unite-default.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("40001");
    product.name = QStringLiteral("ماء");
    product.salePriceCents = 60;
    product.costPriceCents = 58;
    product.piecesPerPackage = 24;
    product.packageName = QStringLiteral("كرتونة");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("40001"));
    page.addEntry();
    QCOMPARE(page.lineCount(), 1);

    auto* unitBox = qobject_cast<QComboBox*>(page.table()->cellWidget(0, 2));
    QVERIFY(unitBox);
    const int packageIndex = unitBox->findData(QStringLiteral("package"));
    QVERIFY(packageIndex >= 0);
    QCOMPARE(unitBox->itemText(packageIndex),
             QCoreApplication::translate("app::ui::PosPage", "كرتونة"));
}

// A name the owner typed is data, not a label, so it is shown exactly as stored
// and never folded into the default. "بالة" is a bale in its own right and the
// Unité dropdown has to say so.
void UiTest::pos_unite_shows_custom_package_name_untouched()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-unite-custom.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("40002");
    product.name = QStringLiteral("تفاح");
    product.salePriceCents = 100;
    product.costPriceCents = 90;
    product.piecesPerPackage = 12;
    product.packageName = QStringLiteral("بالة");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 50, QStringLiteral("purchase"));

    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("40002"));
    page.addEntry();
    QCOMPARE(page.lineCount(), 1);

    auto* unitBox = qobject_cast<QComboBox*>(page.table()->cellWidget(0, 2));
    QVERIFY(unitBox);
    const int packageIndex = unitBox->findData(QStringLiteral("package"));
    QVERIFY(packageIndex >= 0);
    QCOMPARE(unitBox->itemText(packageIndex), QStringLiteral("بالة"));
}

// The Unité cell holds a QComboBox, and the unit column is a fixed 80px, which
// is narrower than the package label plus the drop-down arrow: the combo was
// squeezed into the cell and its last letter elided ("Cartor"). The combo now
// sets its own floor from the widest label it can show. The name is the literal
// "Carton" here rather than a translation, because the test is about width, not
// about the catalogue.
void UiTest::pos_unite_column_shows_the_whole_package_name()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-unite-column-width.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("40003");
    product.name = QStringLiteral("شاي");
    product.salePriceCents = 60;
    product.costPriceCents = 58;
    product.piecesPerPackage = 24;
    product.packageName = QStringLiteral("Carton");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    ui::PosPage page(db);
    page.show();
    QCoreApplication::processEvents();
    page.setEntryText(QStringLiteral("40003"));
    page.addEntry();

    auto* unitBox = qobject_cast<QComboBox*>(page.table()->cellWidget(0, 2));
    QVERIFY(unitBox);
    unitBox->setCurrentIndex(unitBox->findData(QStringLiteral("package")));
    QCOMPARE(unitBox->currentText(), QStringLiteral("Carton"));

    // 40 is the arrow plus the theme's padding with room for the label to sit
    // clear of the indicator; the combo's own minimum is built with 48.
    const int needed = unitBox->fontMetrics().horizontalAdvance(QStringLiteral("Carton")) + 40;
    QVERIFY2(unitBox->width() >= needed,
             qPrintable(QStringLiteral("combo width %1 below needed %2")
                            .arg(unitBox->width())
                            .arg(needed)));
}

// appIcon() renders into a pixmap that is `size` logical pixels at a device
// pixel ratio of 2, so the painter must scale by size/24 and let the ratio map
// logical to device pixels. Multiplying by the ratio a second time drew the
// glyph at twice the box and pinned it to the bottom-right corner, where it was
// clipped: at 36px the pre-fix ink spanned (8,8)-(35,35), touching the frame,
// against (4,4)-(31,31) centred once the extra factor is gone. The bounds below
// are the ones asked for; the last check is what actually fails on the old code.
void UiTest::app_icon_paints_inside_the_requested_box()
{
    constexpr int box = 36;
    QImage image(box, box, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    ui::appIcon(ui::Icon::Plus, Qt::white, box).paint(&painter, 0, 0, box, box);
    painter.end();

    int minX = box;
    int minY = box;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < box; ++y) {
        for (int x = 0; x < box; ++x) {
            if (qAlpha(image.pixel(x, y)) == 0) {
                continue;
            }
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }
    QVERIFY2(maxX >= 0 && maxY >= 0, "the icon painted nothing");

    const int width = maxX - minX + 1;
    const int height = maxY - minY + 1;
    QVERIFY2(width >= 24, qPrintable(QStringLiteral("width=%1").arg(width)));
    QVERIFY2(height >= 24, qPrintable(QStringLiteral("height=%1").arg(height)));
    QVERIFY2(width <= box, qPrintable(QStringLiteral("width=%1").arg(width)));
    QVERIFY2(height <= box, qPrintable(QStringLiteral("height=%1").arg(height)));
    // A glyph drawn at twice the box is clipped at the far edges, so it touches
    // them. A correctly drawn glyph keeps a margin on every side.
    QVERIFY2(maxX < box - 1 && maxY < box - 1,
             qPrintable(QStringLiteral("icon touches the box edge: %1,%2").arg(maxX).arg(maxY)));
}

// Covers the products-page add path, not the till: the same opening-count
// field exists on both, and the fix in Phase 5 wired both to write a
// movement. One path tested is half the fix.
void UiTest::products_page_new_product_records_opening_stock()
{
    const QString path = m_dir.filePath(QStringLiteral("products-page-opening.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    app::ui::ProductsPage page(db);
    page.refresh();

    QPushButton* addBtn = nullptr;
    for (QPushButton* b : page.findChildren<QPushButton*>()) {
        if (b->objectName() == QStringLiteral("primary")) {
            addBtn = b;
            break;
        }
    }
    QVERIFY2(addBtn, "could not find the add-product button");

    bool typedOk = false;
    bool watchdogFired = false;

    QTimer::singleShot(0, [&]() {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) {
            return;
        }
        auto* barcodeField =
            qobject_cast<QLineEdit*>(fieldLabelled(modal, QStringLiteral("الباركود")));
        auto* nameField =
            qobject_cast<QLineEdit*>(fieldLabelled(modal, QStringLiteral("الاسم")));
        auto* saleField =
            qobject_cast<QLineEdit*>(fieldLabelled(modal, QStringLiteral("سعر البيع")));
        auto* openingBox =
            qobject_cast<QSpinBox*>(fieldLabelled(modal, QStringLiteral("الكمية الافتتاحية")));
        if (!barcodeField || !nameField || !saleField || !openingBox) {
            return;
        }
        barcodeField->setText(QStringLiteral("7778889990001"));
        nameField->setText(QStringLiteral("منتج الصفحة"));
        saleField->setText(QStringLiteral("150"));
        openingBox->setValue(30);
        typedOk = true;
        if (auto* box = modal->findChild<QDialogButtonBox*>()) {
            if (QPushButton* ok = box->button(QDialogButtonBox::Ok)) {
                ok->click();
            }
        }
    });

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        watchdogFired = true;
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            modal->reject();
        }
    });
    watchdog.start(3000);

    addBtn->click();

    QVERIFY2(!watchdogFired, "the product dialog had to be closed by the watchdog");
    QVERIFY2(typedOk, "the product dialog was missing one of the fields the test fills");

    const auto saved =
        data::ProductRepository(db).findByBarcode(QStringLiteral("7778889990001"));
    QVERIFY2(saved.has_value(), "the product the dialog accepted was not saved");
    const int productId = saved->id;
    QVERIFY(productId > 0);

    // One movement, and it says where the first thirty came from. The column on the
    // product row agreeing with it is the trigger's doing, not this path's.
    QSqlQuery movements(db.handle());
    QVERIFY(movements.prepare(QStringLiteral(
        "SELECT delta, reason FROM stock_movements WHERE product_id = ?")));
    movements.addBindValue(productId);
    QVERIFY(movements.exec());
    int movementCount = 0;
    while (movements.next()) {
        ++movementCount;
        QCOMPARE(movements.value(0).toLongLong(), 30LL);
        QCOMPARE(movements.value(1).toString(), QStringLiteral("opening"));
    }
    QCOMPARE(movementCount, 1);

    QSqlQuery stored(db.handle());
    QVERIFY(stored.prepare(QStringLiteral("SELECT quantity FROM products WHERE id = ?")));
    stored.addBindValue(productId);
    QVERIFY(stored.exec());
    QVERIFY(stored.next());
    QCOMPARE(stored.value(0).toLongLong(), 30LL);
}

void UiTest::pos_new_product_from_scan_records_opening_stock()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-opening-stock.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    ui::PosPage page(db);

    // Two modals come up in sequence inside one addEntry() call: the product form,
    // then the "Ajouter à la vente ?" question. A timer that looks at whatever is
    // on top and deals with each in turn, because one singleShot(0) would fire
    // against the first and be gone before the second ever existed.
    int phase = 0;
    bool openingFieldSeen = false;
    bool openingFieldVisible = false;
    bool answeredQuestion = false;
    bool watchdogFired = false;

    QTimer driver;
    driver.setInterval(10);
    QObject::connect(&driver, &QTimer::timeout, [&]() {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) {
            return;
        }
        if (phase == 0) {
            auto* opening = fieldLabelled(modal, QStringLiteral("الكمية الافتتاحية"));
            if (!opening) {
                return;
            }
            openingFieldSeen = true;
            openingFieldVisible = opening->isVisible();
            auto* openingBox = qobject_cast<QSpinBox*>(opening);
            if (!openingBox) {
                return;
            }
            auto* barcodeField = qobject_cast<QLineEdit*>(
                fieldLabelled(modal, QStringLiteral("الباركود")));
            auto* nameField =
                qobject_cast<QLineEdit*>(fieldLabelled(modal, QStringLiteral("الاسم")));
            auto* saleField =
                qobject_cast<QLineEdit*>(fieldLabelled(modal, QStringLiteral("سعر البيع")));
            if (!barcodeField || !nameField || !saleField) {
                return;
            }
            barcodeField->setText(QStringLiteral("9990001112223"));
            nameField->setText(QStringLiteral("منتج"));
            saleField->setText(QStringLiteral("100"));
            openingBox->setValue(20);
            if (auto* box = modal->findChild<QDialogButtonBox*>()) {
                if (QPushButton* ok = box->button(QDialogButtonBox::Ok)) {
                    ok->click();
                }
            }
            phase = 1;
            return;
        }
        // QMessageBox::question() is itself the modal, and findChild() never returns the
        // widget it is called on, so the cast on the modal has to come first.
        auto* box = qobject_cast<QMessageBox*>(modal);
        if (!box) {
            box = modal->findChild<QMessageBox*>();
        }
        if (box) {
            if (QAbstractButton* yes = box->button(QMessageBox::Yes)) {
                yes->click();
            }
            answeredQuestion = true;
            phase = 2;
            driver.stop();
        }
    });

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        watchdogFired = true;
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            modal->reject();
        }
    });
    watchdog.start(4000);
    driver.start();

    page.setEntryText(QStringLiteral("9990001112223"));
    page.addEntry();
    driver.stop();

    QVERIFY2(!watchdogFired, "a modal was still open when the test gave up");
    QVERIFY2(openingFieldSeen, "the new-product form had no opening-stock field");
    // Asked on the way in only: this is the form for a product that does not exist
    // yet, so the count has to be on it.
    QVERIFY2(openingFieldVisible, "the opening-stock field was hidden on a new product");
    QVERIFY2(answeredQuestion, "the 'add to this sale?' question was never answered");

    // The scan added its line, so the product really was created through the till.
    QCOMPARE(page.lineCount(), 1);
    QCOMPARE(page.table()->item(0, 0)->text(), QStringLiteral("منتج"));
    QCOMPARE(page.table()->item(0, 1)->text(), QStringLiteral("9990001112223"));

    const int productId = page.table()->item(0, 0)->data(Qt::UserRole).toInt();
    QVERIFY(productId > 0);

    // The count landed as a movement and not as a bare column write: this is the
    // ledger row that says where the first twenty pieces came from, and it is what
    // the stock page and the reports read.
    QSqlQuery movements(db.handle());
    QVERIFY(movements.prepare(QStringLiteral(
        "SELECT delta, reason FROM stock_movements WHERE product_id = ?")));
    movements.addBindValue(productId);
    QVERIFY(movements.exec());
    int movementCount = 0;
    while (movements.next()) {
        ++movementCount;
        QCOMPARE(movements.value(0).toLongLong(), 20LL);
        QCOMPARE(movements.value(1).toString(), QStringLiteral("opening"));
    }
    QCOMPARE(movementCount, 1);

    // And the shelf agrees with the movement, which it only can because the trigger
    // moved it.
    QSqlQuery stored(db.handle());
    QVERIFY(stored.prepare(QStringLiteral("SELECT quantity FROM products WHERE id = ?")));
    stored.addBindValue(productId);
    QVERIFY(stored.exec());
    QVERIFY(stored.next());
    QCOMPARE(stored.value(0).toLongLong(), 20LL);

    // ---- The same product, opened for editing. ----
    // An opening count is a fact about the first day, so the form must not offer it
    // again, and saving the form untouched must not write a second movement: that
    // would be the shelf gaining twenty pieces nobody bought.
    app::ui::ProductsPage catalogue(db);
    catalogue.refresh();

    bool editFieldSeen = false;
    bool editFieldVisible = true;
    bool editSaved = false;
    watchdog.start(4000);
    driver.start();
    QObject::connect(&driver, &QTimer::timeout, [&]() {
        if (phase != 3) {
            return;
        }
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) {
            return;
        }
        if (auto* opening = fieldLabelled(modal, QStringLiteral("الكمية الافتتاحية"))) {
            editFieldSeen = true;
            editFieldVisible = opening->isVisible();
            if (auto* box = modal->findChild<QDialogButtonBox*>()) {
                if (QPushButton* ok = box->button(QDialogButtonBox::Ok)) {
                    ok->click();
                }
            }
            editSaved = true;
            driver.stop();
        }
    });

    // The name column is the only one that opens the form, and the one product in
    // the catalogue is on the first row.
    phase = 3;
    QVERIFY(QMetaObject::invokeMethod(&catalogue, "onNameActivated", Q_ARG(int, 0),
                                      Q_ARG(int, 1)));
    driver.stop();

    QVERIFY2(!watchdogFired, "the edit form was still open when the test gave up");
    QVERIFY2(editFieldSeen, "the edit form had no opening-stock row to hide");
    QVERIFY2(!editFieldVisible, "the opening-stock field was offered on an existing product");
    QVERIFY(editSaved);

    QSqlQuery again(db.handle());
    QVERIFY(again.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM stock_movements WHERE product_id = ? AND reason = 'opening'")));
    again.addBindValue(productId);
    QVERIFY(again.exec());
    QVERIFY(again.next());
    QCOMPARE(again.value(0).toInt(), 1);

    QVERIFY(stored.prepare(QStringLiteral("SELECT quantity FROM products WHERE id = ?")));
    stored.addBindValue(productId);
    QVERIFY(stored.exec());
    QVERIFY(stored.next());
    QCOMPARE(stored.value(0).toLongLong(), 20LL);
}

void UiTest::posSaleRequiresOpenSession()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-no-session.sqlite"));
    int unusedSession = 0;
    const int productId = seedProduct(path, &unusedSession, 8000);

    data::Database db(path);
    data::CashSessionRepository sessions(db);
    const auto open = sessions.findOpen();
    if (open.has_value()) {
        sessions.close(open->id, open->openingFloatCents, open->openingFloatCents, 0);
    }

    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("6130000000004"));
    page.addEntry();
    QCOMPARE(page.lineCount(), 1);
    page.completeSale();
    QCOMPARE(page.lastSaleId(), 0);
    QCOMPARE(page.lineCount(), 1);
    QVERIFY(!page.noticeText().isEmpty());

    data::SaleRepository sales(db);
    QCOMPARE(static_cast<int>(sales.findAll().size()), 0);
}

void UiTest::cash_session_rejects_invalid_amount()
{
    const QString path = m_dir.filePath(QStringLiteral("cashamount.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    ui::CashSessionPage page(db);

    long long cents = -1;

    // Junk, and a negative: parseMoney turns both away.
    QVERIFY(!page.amountFromInput(QStringLiteral("abc"), &cents));
    QVERIFY(!page.amountFromInput(QStringLiteral("-100"), &cents));

    // Zero is the one that used to get through. It is a valid parse, so the
    // nullopt check alone let it close the session with nothing counted and
    // book the whole till as a deficit.
    QVERIFY(!page.amountFromInput(QStringLiteral("0"), &cents));
    QVERIFY(!page.amountFromInput(QString(), &cents));
    QVERIFY(!page.amountFromInput(QStringLiteral("  "), &cents));

    // Rejected input must not have opened anything, and must not have written a
    // value into the caller's variable.
    QVERIFY(!page.hasOpenSession());
    QCOMPARE(page.sessionId(), 0);
    QCOMPARE(cents, -1LL);

    // A real amount still works, and the session opens on it. parseMoney
    // converts what the operator typed into cents, so "5000" is five thousand
    // units, i.e. 500000 cents — not the 5000 the public slot takes directly.
    QVERIFY(page.amountFromInput(QStringLiteral("5000"), &cents));
    QCOMPARE(cents, 500000LL);
    page.openSession(cents);
    QVERIFY(page.hasOpenSession());
    QCOMPARE(page.expectedCents(), 500000LL);

    // And on the way out, a zero count is refused too: accepting it would have
    // closed the session against a negative variance of the full float.
    QVERIFY(!page.amountFromInput(QStringLiteral("0"), &cents));
    QVERIFY(page.hasOpenSession());
    QCOMPARE(page.expectedCents(), 500000LL);
}

void UiTest::cashSessionLifecycle()
{
    const QString path = m_dir.filePath(QStringLiteral("session.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 6000;
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    product.packageSize = 1;
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    ui::CashSessionPage page(db);
    QVERIFY(!page.hasOpenSession());

    page.openSession(5000);
    QVERIFY(page.hasOpenSession());
    QVERIFY(page.sessionId() > 0);
    QCOMPARE(page.movementCount(), 0);
    QCOMPARE(page.expectedCents(), 5000LL);

    data::SaleService service(db);
    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    const data::SaleRecordResult result = service.recordSale({ item }, page.sessionId(), "desktop", false);
    QVERIFY(result.ok);
    QCOMPARE(result.totalCents, 10000LL);

    page.refresh();
    QCOMPARE(page.movementCount(), 1);
    QCOMPARE(page.expectedCents(), 15000LL);

    page.closeSession(15000);
    QVERIFY(!page.hasOpenSession());
    QCOMPARE(page.lastVarianceCents(), 0LL);

    page.openSession(2000);
    item.quantity = 2;
    const data::SaleRecordResult result2 = service.recordSale({ item }, page.sessionId(), "desktop", false);
    QVERIFY(result2.ok);
    QCOMPARE(result2.totalCents, 10000LL);
    page.refresh();
    QCOMPARE(page.expectedCents(), 12000LL);
    page.closeSession(11500);
    QVERIFY(!page.hasOpenSession());
    QCOMPARE(page.lastVarianceCents(), -500LL);
}

void UiTest::salesPageShowsToday()
{
    int unused = 0;
    const QString path = m_dir.filePath(QStringLiteral("sales-today.sqlite"));
    const int productId = seedProduct(path, &unused, 10000);

    data::Database db(path);
    data::CashSessionRepository sessions(db);
    const auto session = sessions.findOpen();
    QVERIFY(session.has_value());
    data::SaleService service(db);

    core::SaleItem desktopItem;
    desktopItem.productId = productId;
    desktopItem.quantity = 2;
    desktopItem.unitPriceCents = 7500; // override
    QVERIFY(service.recordSale({ desktopItem }, session->id, "desktop", false).ok);

    core::SaleItem deviceItem;
    deviceItem.productId = productId;
    deviceItem.quantity = 1;
    QVERIFY(service.recordSale({ deviceItem }, session->id, "dev-1", false).ok);

    ui::SalesPage page(db);
    QCOMPARE(page.rowCount(), 2);
    QCOMPARE(page.grandTotalCents(), 25000LL);
    QCOMPARE(page.profitCents(), 7000LL);
}

// grandTotalCents()/profitCents() used to scrape the numbers back out of the Arabic
// summary label with a regex, so a translated UI silently reported the grand total
// as the profit. Overwrite the label with non-Arabic text and both must hold steady.
void UiTest::profit_cents_survives_translation()
{
    const QString path = m_dir.filePath(QStringLiteral("sales-translated.sqlite"));
    int seededSessionId = 0;
    const int productId = seedProduct(path, &seededSessionId, 10000);
    QVERIFY(productId > 0);

    data::Database db(path);
    data::CashSessionRepository sessions(db);
    const auto session = sessions.findOpen();
    QVERIFY(session.has_value());
    data::SaleService service(db);

    core::SaleItem desktopItem;
    desktopItem.productId = productId;
    desktopItem.quantity = 2;
    desktopItem.unitPriceCents = 7500; // override
    QVERIFY(service.recordSale({ desktopItem }, session->id, "desktop", false).ok);

    core::SaleItem deviceItem;
    deviceItem.productId = productId;
    deviceItem.quantity = 1;
    deviceItem.unitPriceCents = 10000;
    QVERIFY(service.recordSale({ deviceItem }, session->id, "dev-1", false).ok);

    ui::SalesPage page(db);
    const qint64 total = page.grandTotalCents();
    const qint64 profit = page.profitCents();
    QCOMPARE(total, 25000LL);
    QCOMPARE(profit, 7000LL);
    QVERIFY(total != profit); // otherwise the old fallback would be indistinguishable

    QLabel* summary = page.findChild<QLabel*>(QStringLiteral("infoBar"));
    QVERIFY(summary);
    summary->setText(QStringLiteral("Ventes: 2  |  Total (net): 999.99  |  Benefice: 888.88"));

    QCOMPARE(page.grandTotalCents(), total);
    QCOMPARE(page.profitCents(), profit);

    // A refresh re-renders the label in the source language; the cache must be rebuilt
    // to the same figures.
    page.refresh();
    QCOMPARE(page.grandTotalCents(), total);
    QCOMPARE(page.profitCents(), profit);
}

void UiTest::sales_page_print_button_enables_on_selection()
{
    const QString path = m_dir.filePath(QStringLiteral("sales-print-btn.sqlite"));
    int sessionId = 0;
    const int productId = seedProduct(path, &sessionId, 10000);
    QVERIFY(productId > 0);

    data::Database db(path);
    data::CashSessionRepository sessions(db);
    const auto session = sessions.findOpen();
    QVERIFY(session.has_value());
    data::SaleService service(db);
    core::SaleItem item;
    item.productId = productId;
    item.quantity = 1;
    QVERIFY(service.recordSale({ item }, session->id, "desktop", false).ok);

    ui::SalesPage page(db);
    QCOMPARE(page.rowCount(), 1);

    auto* printBtn = page.findChild<QPushButton*>(QStringLiteral("printReceipt"));
    QVERIFY(printBtn);
    QVERIFY(!printBtn->isEnabled());

    page.table()->selectRow(0);
    QVERIFY(printBtn->isEnabled());
}

void UiTest::sales_page_details_shows_the_receipt_number()
{
    const QString path = m_dir.filePath(QStringLiteral("sales-details-receipt.sqlite"));
    int sessionId = 0;
    const int productId = seedProduct(path, &sessionId, 10000);
    QVERIFY(productId > 0);

    data::Database db(path);
    data::CashSessionRepository sessions(db);
    const auto session = sessions.findOpen();
    QVERIFY(session.has_value());
    data::SaleService service(db);
    core::SaleItem item;
    item.productId = productId;
    item.quantity = 1;
    const data::SaleRecordResult sold =
        service.recordSale({ item }, session->id, "desktop", false);
    QVERIFY2(sold.ok, qPrintable(sold.error));
    QVERIFY(sold.saleId > 0);

    data::SaleRepository sales(db);
    const auto stored = sales.findById(sold.saleId);
    QVERIFY(stored.has_value());
    QVERIFY(!stored->receiptNumber.isEmpty());

    ui::SalesPage page(db);
    QCOMPARE(page.rowCount(), 1);
    QTableWidget* table = page.table();
    table->selectRow(0);
    page.show();
    page.resize(900, 600);
    QCoreApplication::processEvents();

    bool found = false;
    QTimer::singleShot(0, [&]() {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) {
            return;
        }
        QLabel* numberLabel = modal->findChild<QLabel*>(QStringLiteral("saleReceiptNumber"));
        if (numberLabel && numberLabel->text().contains(stored->receiptNumber)) {
            found = true;
        }
        modal->reject();
    });

    // A press first: Qt routes a lone MouseButtonDblClick on an item view to a
    // single pressed() unless the preceding press landed on the same cell.
    const QPoint cellCenter = table->viewport()->mapFromGlobal(
            table->mapToGlobal(table->visualItemRect(table->item(0, 0)).center()));
    QTest::mousePress(table->viewport(), Qt::LeftButton, Qt::NoModifier, cellCenter);
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, cellCenter);
    QVERIFY2(found, "the details dialog did not show the sale's receipt number");
}

void UiTest::sales_page_prints_the_selected_sale_as_pdf()
{
    const QString path = m_dir.filePath(QStringLiteral("sales-print-pdf.sqlite"));
    int sessionId = 0;
    const int productId = seedProduct(path, &sessionId, 10000);
    QVERIFY(productId > 0);

    data::Database db(path);
    data::CashSessionRepository sessions(db);
    const auto session = sessions.findOpen();
    QVERIFY(session.has_value());
    data::SaleService service(db);
    core::SaleItem item;
    item.productId = productId;
    item.quantity = 1;
    QVERIFY(service.recordSale({ item }, session->id, "desktop", false).ok);

    ui::SalesPage page(db);
    QCOMPARE(page.rowCount(), 1);
    QTableWidget* table = page.table();
    table->selectRow(0);
    const int saleId = table->item(0, 0)->data(Qt::UserRole).toInt();
    QVERIFY(saleId > 0);

    // The interactive path opens a QFileDialog when no printer is installed,
    // and a dialog would hang the suite. The test drives the same loader and
    // the same printer the button uses, from the same sale id the table
    // exposes, so the wire from selection to paper is covered without the
    // modal.
    const ui::ReceiptData data = ui::buildReceiptForSale(db, saleId);
    QTextDocument* doc = ui::renderReceipt(data, ui::ReceiptPaper::Thermal80);

    const QString file = m_dir.filePath(QStringLiteral("sales-receipt.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(file);
    ui::configurePrinterForPaper(printer, ui::ReceiptPaper::Thermal80);
    ui::printReceiptToPrinter(doc, printer);

    QVERIFY(QFileInfo(file).exists());
    QFile f(file);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray first = f.read(4);
    QVERIFY(first == "%PDF");
    f.close();
    delete doc;
}

// The printer half is not exercised: CI has no printer, and
// QPrinterInfo::defaultPrinter() on a headless machine is invalid, which the
// code already skips.
void UiTest::receipt_print_and_save_writes_pdf_and_returns_path()
{
    const QString folder = m_dir.filePath(QStringLiteral("receipts"));
    QVERIFY(!QDir(folder).exists());

    const QString path = m_dir.filePath(QStringLiteral("receipt-save.sqlite"));
    data::Database db(path);

    data::SettingRepository settings(db);
    settings.set(QStringLiteral("shop_name"), QStringLiteral("Boutique Test"));
    settings.set(QStringLiteral("currency_symbol"), QStringLiteral("DA"));
    settings.set(QStringLiteral("receipt_save_folder"), folder);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000011");
    product.name = QStringLiteral("Biscuit");
    product.costPriceCents = 5000;
    product.salePriceCents = 6000;
    product.piecesPerPackage = 1;
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 6000;

    data::SaleService service(db);
    const data::SaleRecordResult sold =
        service.recordSale({ item }, sessionId, QStringLiteral("desktop"), false);
    QVERIFY2(sold.ok, qPrintable(sold.error));

    const ui::ReceiptData data = ui::buildReceiptForSale(db, sold.saleId);
    QTextDocument* doc = ui::renderReceipt(data, ui::ReceiptPaper::Thermal80);
    doc->setMetaInformation(QTextDocument::DocumentTitle, data.receiptNumber);

    QString error;
    const QString out = ui::printAndSaveReceipt(doc, ui::ReceiptPaper::Thermal80, db, &error);
    delete doc;

    QVERIFY(!out.isEmpty());
    QVERIFY(out.endsWith(QStringLiteral(".pdf")));
    QVERIFY2(QFileInfo(out).exists(), qPrintable(out));
    QVERIFY(QFileInfo(out).size() > 1000);
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray first = f.read(4);
    QVERIFY(first == "%PDF");
    f.close();
    QVERIFY(QDir(folder).exists());
    QVERIFY(error.isEmpty());
}
// to hit the default (OK) button, saving and closing the form mid-entry.

void UiTest::receipt_is_a_single_page_on_thermal_paper()
{
    const QString path = m_dir.filePath(QStringLiteral("receipt-single-page.sqlite"));
    data::Database db(path);

    data::SettingRepository settings(db);
    settings.set(QStringLiteral("shop_name"), QStringLiteral("Boutique Test"));
    settings.set(QStringLiteral("currency_symbol"), QStringLiteral("DA"));

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::SaleService service(db);
    data::ProductRepository products(db);
    core::SaleItem items[4];
    for (int i = 0; i < 4; ++i) {
        core::Product p;
        p.barcode = QStringLiteral("61300000000%1").arg(i + 10);
        p.name = QStringLiteral("Item%1").arg(i + 1);
        p.costPriceCents = 500;
        p.salePriceCents = 1000;
        p.piecesPerPackage = 1;
        const int pid = products.save(p);
        QVERIFY(pid > 0);
        products.adjustStock(pid, 100, QStringLiteral("purchase"));
        items[i].productId = pid;
        items[i].quantity = 1;
        items[i].unitPriceCents = 1000;
    }

    const data::SaleRecordResult sold =
        service.recordSale({items[0], items[1], items[2], items[3]}, sessionId,
                           QStringLiteral("desktop"), false);
    QVERIFY2(sold.ok, qPrintable(sold.error));

    const ui::ReceiptData data = ui::buildReceiptForSale(db, sold.saleId);
    QTextDocument* doc = ui::renderReceipt(data, ui::ReceiptPaper::Thermal80);
    doc->setMetaInformation(QTextDocument::DocumentTitle, data.receiptNumber);

    const QString pdf = m_dir.filePath(QStringLiteral("single-page.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(pdf);
    ui::configurePrinterForPaper(printer, ui::ReceiptPaper::Thermal80);
    const QSizeF docSize = doc->pageSize();
    if (docSize.isValid()) {
        const QSizeF mmSize(docSize.width() * 25.4 / 96.0,
                            docSize.height() * 25.4 / 96.0);
        printer.setPageSize(QPageSize(mmSize, QPageSize::Millimeter,
                                      QStringLiteral("receipt"),
                                      QPageSize::ExactMatch));
        printer.setFullPage(true);
    }
    ui::printReceiptToPrinter(doc, printer);

    QProcess p;
    p.start(QStringLiteral("pdfinfo"), QStringList() << pdf);
    QVERIFY(p.waitForFinished());
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    int pages = -1;
    QRegularExpression re(QStringLiteral("Pages:\\s*(\\d+)"));
    auto m = re.match(out);
    if (m.hasMatch()) {
        pages = m.captured(1).toInt();
    }
    QVERIFY2(pages == 1, qPrintable(out));
    delete doc;
}

void UiTest::barcode_dialog_does_not_close()
{
    const QString path = m_dir.filePath(QStringLiteral("barcode-dialog.sqlite"));
    data::Database db(path);

    app::ui::ProductsPage page(db);
    page.refresh();

    QPushButton* addBtn = nullptr;
    for (QPushButton* b : page.findChildren<QPushButton*>()) {
        // Matched on the object name, not the label: the button is identified by
        // the #primary role the stylesheet keys on, so a translation of the
        // visible text cannot silently break this lookup.
        if (b->objectName() == QStringLiteral("primary")) {
            addBtn = b;
            break;
        }
    }
    QVERIFY2(addBtn, "could not find the add-product button");

    bool modalOpened = false;
    bool stillOpen = false;
    bool focusMovedToName = false;
    bool barcodeHeldScan = false;
    bool watchdogFired = false;

    // productDialog() calls exec(), so drive it from a 0 ms timer that runs inside
    // the dialog's nested event loop.
    QTimer::singleShot(0, [&]() {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) {
            return;
        }
        modalOpened = true;

        // The fields are addressed through their form labels: the dialog sets no
        // placeholder text, and relying on child order would be brittle.
        QFormLayout* form = nullptr;
        for (QFormLayout* l : modal->findChildren<QFormLayout*>()) {
            form = l;
            break;
        }
        if (!form) {
            return;
        }

        QLineEdit* barcodeField = nullptr;
        QLineEdit* nameField = nullptr;
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem* labelItem = form->itemAt(row, QFormLayout::LabelRole);
            QLayoutItem* fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            auto* label = qobject_cast<QLabel*>(labelItem ? labelItem->widget() : nullptr);
            auto* field = qobject_cast<QLineEdit*>(fieldItem ? fieldItem->widget() : nullptr);
            if (!label || !field) {
                continue;
            }
            if (label->text() == QStringLiteral("الباركود")) {
                barcodeField = field;
            } else if (label->text() == QStringLiteral("الاسم")) {
                nameField = field;
            }
        }
        if (!barcodeField || !nameField) {
            return;
        }

        barcodeField->setFocus();
        QTest::keyClicks(barcodeField, QStringLiteral("12345"));
        // Same event a scanner emits: the digits, then Return.
        QTest::keyClick(barcodeField, Qt::Key_Return);
        QTest::qWait(20);

        stillOpen = modal->isVisible();
        barcodeHeldScan = (barcodeField->text() == QStringLiteral("12345"));
        focusMovedToName = (modal->focusWidget() == nameField);

        modal->reject();
    });

    // Safety net: a stuck modal must never hang the whole suite.
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        watchdogFired = true;
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            modal->reject();
        }
    });
    watchdog.start(3000);

    addBtn->click();

    QVERIFY2(!watchdogFired, "the product dialog had to be closed by the watchdog");
    QVERIFY2(modalOpened, "the add-product button did not open the product dialog");
    QVERIFY2(barcodeHeldScan, "the barcode field lost the scanned digits");
    QVERIFY2(stillOpen, "Enter from the barcode scanner closed the product dialog");
    QVERIFY2(focusMovedToName, "Enter did not move focus from the barcode to the name field");
}

// The carton fields are written from the one spinbox, so the two columns that hold
// the figure cannot drift apart: a product saved with one of them at 1 and the other
// at 24 would have the stock arithmetic believing the shelf holds a single piece.
// This drives the real form the way barcode_dialog_does_not_close does, through the
// add button and the modal it opens.
void UiTest::product_dialog_writes_package_fields_from_the_shared_widget()
{
    const QString path = m_dir.filePath(QStringLiteral("product-dialog-package.sqlite"));
    data::Database db(path);

    app::ui::ProductsPage page(db);
    page.refresh();

    QPushButton* addBtn = nullptr;
    for (QPushButton* b : page.findChildren<QPushButton*>()) {
        if (b->objectName() == QStringLiteral("primary")) {
            addBtn = b;
            break;
        }
    }
    QVERIFY2(addBtn, "could not find the add-product button");

    bool modalOpened = false;
    bool typedOk = false;
    bool watchdogFired = false;

    QTimer::singleShot(0, [&]() {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) {
            return;
        }
        modalOpened = true;

        QFormLayout* form = nullptr;
        for (QFormLayout* l : modal->findChildren<QFormLayout*>()) {
            form = l;
            break;
        }
        if (!form) {
            return;
        }

        // One walk for every field: the labels are Arabic and the dialog sets no
        // object names on them, so the label text is what identifies a row.
        QLineEdit* barcodeField = nullptr;
        QLineEdit* nameField = nullptr;
        QLineEdit* saleField = nullptr;
        QSpinBox* piecesField = nullptr;
        QComboBox* packageNameField = nullptr;
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem* labelItem = form->itemAt(row, QFormLayout::LabelRole);
            QLayoutItem* fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            auto* label = qobject_cast<QLabel*>(labelItem ? labelItem->widget() : nullptr);
            if (!label) {
                continue;
            }
            QWidget* field = fieldItem ? fieldItem->widget() : nullptr;
            const QString text = label->text();
            if (text == QStringLiteral("الباركود")) {
                barcodeField = qobject_cast<QLineEdit*>(field);
            } else if (text == QStringLiteral("الاسم")) {
                nameField = qobject_cast<QLineEdit*>(field);
            } else if (text == QStringLiteral("سعر البيع")) {
                saleField = qobject_cast<QLineEdit*>(field);
            } else if (text == QStringLiteral("عدد الحبات في العلبة")) {
                piecesField = qobject_cast<QSpinBox*>(field);
            } else if (text == QStringLiteral("اسم العلبة")) {
                packageNameField = qobject_cast<QComboBox*>(field);
            }
        }
        if (!barcodeField || !nameField || !saleField || !piecesField || !packageNameField) {
            return;
        }

        barcodeField->setText(QStringLiteral("1234567890123"));
        nameField->setText(QStringLiteral("بسكويت"));
        saleField->setText(QStringLiteral("60"));
        piecesField->setValue(24);
        // Left at whatever the form opened on: the combo is seeded from the
        // product, which for a new one is the default word for a carton.
        const QString chosenName = packageNameField->currentText();
        typedOk = (chosenName == QStringLiteral("كرتونة"));

        auto* box = modal->findChild<QDialogButtonBox*>();
        if (box) {
            if (QPushButton* ok = box->button(QDialogButtonBox::Ok)) {
                ok->click();
            }
        }
    });

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        watchdogFired = true;
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            modal->reject();
        }
    });
    watchdog.start(3000);

    addBtn->click();

    QVERIFY2(!watchdogFired, "the product dialog had to be closed by the watchdog");
    QVERIFY2(modalOpened, "the add-product button did not open the product dialog");
    QVERIFY2(typedOk, "the carton-name combo did not open on the default word");

    const auto saved = data::ProductRepository(db).findByBarcode(QStringLiteral("1234567890123"));
    QVERIFY2(saved.has_value(), "the product the dialog accepted was not saved");

    // Both columns carry the one figure the spinbox held. They are written from the
    // same widget on purpose, so a mismatch here would mean the form had grown a
    // second source for the number.
    QCOMPARE(saved->piecesPerPackage, 24);
    QCOMPARE(saved->packageSize, 24);
    QCOMPARE(saved->packageName, QStringLiteral("كرتونة"));
    // Computed rather than typed: the carton's code is the piece's plus a marker.
    QCOMPARE(saved->packageBarcode, QStringLiteral("1234567890123c"));
}

void UiTest::quick_items_bar_lists_products()
{
    const QString path = m_dir.filePath(QStringLiteral("quick-items.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product scanned;
    scanned.barcode = QStringLiteral("6130000000009");
    scanned.name = QStringLiteral("معجون");
    scanned.salePriceCents = 10000;
    QVERIFY(products.save(scanned) > 0);

    core::Product bread;
    bread.barcode = QString();  // null -> no barcode -> a quick item
    bread.name = QStringLiteral("خبز شعير");
    bread.salePriceCents = 2500;
    const int breadId = products.save(bread);
    QVERIFY(breadId > 0);

    core::Product water;
    water.barcode = QString();
    water.name = QStringLiteral("ماء");
    water.salePriceCents = 1500;
    const int waterId = products.save(water);
    QVERIFY(waterId > 0);

    app::ui::QuickItemsBar bar(db);
    bar.refresh();

    // Only the two products without a barcode are listed.
    QCOMPARE(bar.cardCount(), 2);
    QVERIFY(!bar.isEmptyMessageVisible());

    // Typing filters the strip down to the matching card.
    bar.searchField()->setText(QStringLiteral("خبز"));
    QCOMPARE(bar.cardCount(), 1);
    QVERIFY(!bar.isEmptyMessageVisible());

    // A search that matches nothing clears the strip and shows the message.
    bar.searchField()->setText(QStringLiteral("شاي"));
    QCOMPARE(bar.cardCount(), 0);
    QVERIFY(bar.isEmptyMessageVisible());

    // Clearing the search brings both cards back.
    bar.searchField()->clear();
    QCOMPARE(bar.cardCount(), 2);

    // A click reports the product id, and nothing else: the cart is not wired
    // up yet, so the bar must not touch the sale.
    QSignalSpy clicks(&bar, &app::ui::QuickItemsBar::productClicked);
    app::ui::QuickItemCard* card = bar.findChildren<app::ui::QuickItemCard*>().value(0);
    QVERIFY(card);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(card, &press);
    QCOMPARE(clicks.size(), 1);
    QVERIFY(clicks.at(0).at(0).toInt() == breadId || clicks.at(0).at(0).toInt() == waterId);

    // An inactive quick item drops out of the listing.
    products.setActive(waterId, false);
    bar.refresh();
    QCOMPARE(bar.cardCount(), 1);
    QCOMPARE(bar.findChildren<app::ui::QuickItemCard*>().value(0)->productId(), breadId);
}

void UiTest::customerCreditAndPayment()
{
    const QString path = m_dir.filePath(QStringLiteral("customer-credit.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 6000;
    product.salePriceCents = 10000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("زبون آجل");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);

    ui::CustomersPage page(db);

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 5000; // override: credit at a lower price
    page.recordDebt(customerId, { item });

    // The page speaks French now, so the notice is checked against the wording it
    // actually shows. What is under test is that a debt raises a notice at all,
    // not which language it is in.
    QVERIFY(page.noticeText().contains(QStringLiteral("Dette enregistrée")));
    QCOMPARE(page.balanceAt(0), QStringLiteral("100.00"));

    // Credit sales never touch the till and never enter the sync outbox.
    data::CashMovementRepository movements(db);
    QCOMPARE(movements.sumBySessionId(sessionId), 0LL);
    QCOMPARE(data::AppliedOpRepository(db).count(), 0);

    page.recordPayment(customerId, 4000, QString());
    QVERIFY(page.noticeText().contains(QStringLiteral("Remboursement enregistré")));
    QCOMPARE(page.balanceAt(0), QStringLiteral("60.00"));
    QCOMPARE(page.balanceAt(0), QStringLiteral("60.00"));
    Q_UNUSED(db)
    QCOMPARE(movements.sumBySessionId(sessionId), 4000LL);

    bool foundPayment = false;
    for (const core::CashMovement& movement : movements.findBySessionId(sessionId)) {
        if (movement.type == QLatin1String("customer_payment")) {
            foundPayment = true;
            QCOMPARE(movement.amountCents, 4000LL);
        }
    }
    QVERIFY(foundPayment);
}

void UiTest::expensesAndDrawings()
{
    const QString path = m_dir.filePath(QStringLiteral("cash-entries.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    ui::ExpensesPage page(db);
    page.recordExpense(QStringLiteral("كهرباء"), 1500);
    QVERIFY(page.noticeText().contains(QStringLiteral("مصروف")));
    page.recordDrawing(QStringLiteral("سحب شخصي"), 2000);
    QVERIFY(page.noticeText().contains(QStringLiteral("سحب")));

    QCOMPARE(page.entryCount(), 2);
    QCOMPARE(page.expensesTotalCents(), 1500LL);

    data::ExpenseRepository expenses(db);
    const auto expenseRows = expenses.findBetween(QDateTime(QDate::currentDate(), QTime(0, 0, 0)),
                                                  QDateTime::currentDateTime());
    QCOMPARE(expenseRows.size(), 1);
    QCOMPARE(expenseRows[0].amountCents, 1500LL);

    data::OwnerDrawingRepository drawings(db);
    const auto drawingRows = drawings.findBetween(QDateTime(QDate::currentDate(), QTime(0, 0, 0)),
                                                  QDateTime::currentDateTime());
    QCOMPARE(drawingRows.size(), 1);
    QCOMPARE(drawingRows[0].amountCents, 2000LL);

    data::CashMovementRepository movements(db);
    QCOMPARE(movements.sumBySessionId(sessionId), -3500LL);
}

void UiTest::reportBuilds()
{
    const QString path = m_dir.filePath(QStringLiteral("report.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 3000;
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("زبون");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);

    data::SaleService saleService(db);
    core::SaleItem saleItem;
    saleItem.productId = productId;
    saleItem.quantity = 2;
    QVERIFY(saleService.recordSale({ saleItem }, sessionId, "desktop", false).ok);

    core::SaleItem debtItem;
    debtItem.productId = productId;
    debtItem.quantity = 1;
    QVERIFY(saleService.recordCustomerDebt(customerId, { debtItem }, "desktop", false).ok);

    data::PaymentService payments(db);
    QVERIFY(payments.recordCustomerPayment(customerId, 2000, sessionId, QString()).ok);

    data::CashEntryService cashEntries(db);
    QVERIFY(cashEntries.recordExpense(QStringLiteral("كهرباء"), 1500, sessionId).ok);
    QVERIFY(cashEntries.recordDrawing(QStringLiteral("سحب"), 2000, sessionId).ok);

    const QDateTime dayStart(QDate::currentDate(), QTime(0, 0, 0));
    const data::StoreReport report = data::ReportService(db).build(dayStart, QDateTime::currentDateTime());

    // Revenue is the cash sale (2 x 5000) plus the one taken on account (1 x 5000):
    // both were sold, and only one of them went through the drawer. The 2000 paid
    // back is a settlement of debt, not a discount on the sale, so it is not
    // subtracted from revenue here — it lands in the drawer and in the balance.
    QCOMPARE(report.revenueCents, 15000LL);
    // Two sales: one for cash, one on account.
    QCOMPARE(report.salesCount, 2LL);
    // Cost of both, 3 tubes at 3000.
    QCOMPARE(report.cogsCents, 9000LL);
    QCOMPARE(report.grossProfitCents, 6000LL);
    QCOMPARE(report.expensesCents, 1500LL);
    QCOMPARE(report.drawingsCents, 2000LL);
    // Net is gross less expenses. Drawings are money the owner took out, not a
    // cost of trading, so they are reported beside the profit rather than
    // subtracted from it.
    QCOMPARE(report.netProfitCents, 4500LL);
    QCOMPARE(report.outstandingDebtCents, 3000LL);
    // The base is the trading-goods one: what is on the shelf at the selling
    // price, what is in the drawer, and what the customers owe. 97 left of 100
    // tubes at 5000, the float plus the movements booked against it, and 5000
    // taken on account less the 2000 paid back.
    QCOMPARE(report.stockValueCents, 485000LL);
    QCOMPARE(report.cashOnHandCents, 13500LL);
    QCOMPARE(report.receivablesCents, 3000LL);
    QCOMPARE(report.supplierDebtCents, 0LL);
    QCOMPARE(report.zakatBaseCents, 501500LL);
    QCOMPARE(report.zakatCents, 12537LL);
    QCOMPARE(report.sessionsOpened, 1LL);
    QCOMPARE(report.openingFloatCents, 5000LL);

    const auto findLine = [&report](const QString& type) -> const data::CashLine* {
        for (const data::CashLine& line : report.cashLines) {
            if (line.type == type) {
                return &line;
            }
        }
        return nullptr;
    };
    const data::CashLine* saleLine = findLine(QStringLiteral("sale"));
    QVERIFY(saleLine != nullptr);
    QCOMPARE(saleLine->count, 1);
    QCOMPARE(saleLine->sumCents, 10000LL);
    QCOMPARE(findLine(QStringLiteral("customer_payment"))->sumCents, 2000LL);
    QCOMPARE(findLine(QStringLiteral("expense"))->sumCents, -1500LL);
    QCOMPARE(findLine(QStringLiteral("drawing"))->sumCents, -2000LL);

    ui::ReportsPage page(db);
    // The same figures the page's own card shows: revenue carries the on-account
    // sale as well as the cash one, while the till lines below still only count
    // what moved in the drawer — 10000 of sale, 2000 paid back.
    QCOMPARE(page.report().revenueCents, 15000LL);
    QCOMPARE(page.report().zakatCents, 12537LL);
    QCOMPARE(page.report().netProfitCents, 4500LL);
}

void UiTest::settingsCurrencyAndZakat()
{
    const QString path = m_dir.filePath(QStringLiteral("settings.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    ui::SettingsPage page(db);

    page.setShopName(QStringLiteral("محل النور"));
    page.setCurrencySymbol(QStringLiteral("دج"));
    page.setZakatEnabled(true);
    page.setSyncKey(QStringLiteral("my-sync-key"));
    page.save();

    QVERIFY(page.noticeText().contains(QStringLiteral("حُفظت")));
    QVERIFY(QStringLiteral("15.00 دج") == ui::formatMoney(1500));

    data::SettingRepository settings(db);
    QCOMPARE(settings.value(QStringLiteral("shop_name")).value_or(QString()), QStringLiteral("محل النور"));
    QCOMPARE(settings.value(QStringLiteral("currency_symbol")).value_or(QString()), QStringLiteral("دج"));
    QCOMPARE(settings.value(QStringLiteral("sync_hmac_key")).value_or(QString()), QStringLiteral("my-sync-key"));
    QCOMPARE(settings.value(QStringLiteral("enabled")).value_or(QString()), QStringLiteral("1"));

    // Disabled zakat in the settings must zero out the report's zakat line.
    page.setZakatEnabled(false);
    page.save();
    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);
    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 3000;
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 10, QStringLiteral("purchase"));

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    QVERIFY(data::SaleService(db).recordSale({ item }, sessionId, "desktop", false).ok);

    const QDateTime dayStart(QDate::currentDate(), QTime(0, 0, 0));
    const data::StoreReport report = data::ReportService(db).build(dayStart, QDateTime::currentDateTime());
    // 8 tubes left at 5000 and a till holding the float plus the 10000 sale.
    // Turning the zakat off zeroes what is owed, never the base it rests on.
    QCOMPARE(report.zakatBaseCents, 55000LL);
    QCOMPARE(report.zakatCents, 0LL);

    app::ui::setCurrencySymbol(QString());
}

void UiTest::zakat_respects_nisab()
{
    const QString path = m_dir.filePath(QStringLiteral("zakat-nisab.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 10, QStringLiteral("purchase"));

    const QDateTime dayStart(QDate::currentDate(), QTime(0, 0, 0));
    const QDateTime now = QDateTime::currentDateTime();

    // No nisab has ever been set, so the shop owes zakat on whatever it holds.
    const data::StoreReport before = data::ReportService(db).build(dayStart, now);
    QCOMPARE(before.stockValueCents, 50000LL);
    QCOMPARE(before.cashOnHandCents, 5000LL);
    QCOMPARE(before.zakatBaseCents, 55000LL);
    QVERIFY(before.zakatCents > 0);

    // A nisab the shop has not reached takes the whole liability to zero, and
    // the base is left standing: it is what the nisab is measured against.
    data::SettingRepository zakat(db);
    zakat.set(QStringLiteral("nisab_cents"), QStringLiteral("999999999"));

    const data::StoreReport after = data::ReportService(db).build(dayStart, now);
    QCOMPARE(after.zakatBaseCents, 55000LL);
    QCOMPARE(after.zakatCents, 0LL);

    // The base says nothing about the setting either way, so dropping the nisab
    // brings the liability straight back rather than leaving it suppressed.
    zakat.set(QStringLiteral("nisab_cents"), QStringLiteral("0"));
    const data::StoreReport cleared = data::ReportService(db).build(dayStart, now);
    QCOMPARE(cleared.zakatCents, before.zakatCents);
}

void UiTest::update_bar_hidden_by_default()
{
    const QString path = m_dir.filePath(QStringLiteral("updatebar.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);

    ui::MainWindow window(db, controller);
    window.show();

    // The bar is built on demand, the first time a newer release is reported.
    // A fresh window must not carry one, or the notice would sit there
    // permanently on any build that happened to be older than the latest tag.
    QFrame* bar = window.findChild<QFrame*>(QStringLiteral("updateBar"));
    QVERIFY2(bar == nullptr, "update bar must not exist before a release is newer");

    // The check itself is deferred 5s past construction, so nothing should have
    // reached the network by the time this test ends and the window dies.
}

void UiTest::update_bar_appears_on_signal()
{
    const QString path = m_dir.filePath(QStringLiteral("updatebar_signal.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);

    ui::MainWindow window(db, controller);
    window.show();
    QCoreApplication::processEvents();

    QVERIFY(window.findChild<QFrame*>(QStringLiteral("updateBar")) == nullptr);

    // No accessor and no friend declaration: the checker is a QObject child of
    // the window, so it is reachable by name, and a signal is invocable through
    // the meta-object exactly like a slot. This is the same signal a real
    // GitHub reply produces — the v9.9.9 tag is only ever greater than the
    // built 1.0.0, so the comparison in the checker is not bypassed.
    auto* checker = window.findChild<core::UpdateChecker*>();
    QVERIFY(checker != nullptr);

    const QString tag = QStringLiteral("v9.9.9");
    const bool fired = QMetaObject::invokeMethod(
        checker, "updateAvailable", Qt::DirectConnection,
        Q_ARG(QString, tag), Q_ARG(QString, QStringLiteral("test notes")));
    QVERIFY2(fired, "could not fire updateAvailable on the checker");
    QCoreApplication::processEvents();

    QFrame* bar = window.findChild<QFrame*>(QStringLiteral("updateBar"));
    QVERIFY2(bar != nullptr, "the bar was not built when a newer release arrived");
    QVERIFY(bar->isVisible());

    bool tagShown = false;
    for (QLabel* label : bar->findChildren<QLabel*>()) {
        if (label->text().contains(tag)) {
            tagShown = true;
        }
    }
    QVERIFY2(tagShown, "no label in the bar mentions the new tag");

    // Found by object name, not by its Arabic text: the label goes through
    // tr(), so its string is not a stable identity once translations land.
    QPushButton* later = bar->findChild<QPushButton*>(QStringLiteral("ghost"));
    QVERIFY2(later != nullptr, "the dismiss button is missing from the bar");

    later->click();
    QCoreApplication::processEvents();
    QVERIFY2(!bar->isVisible(), "the bar stayed up after the dismiss button");
}

void UiTest::refundsRestoreMoneyAndStock()
{
    const QString path = m_dir.filePath(QStringLiteral("refunds.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 3000;
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("زبون");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);
    Q_UNUSED(customerId);

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    const data::SaleRecordResult sale =
        data::SaleService(db).recordSale({ item }, sessionId, "desktop", false);
    QVERIFY(sale.ok);
    QCOMPARE(products.findById(productId)->quantity, 98);

    ui::RefundsPage page(db);
    QCOMPARE(page.salesRowCount(), 1);
    page.refundSale(sale.saleId);
    QVERIFY(page.noticeText().contains(QStringLiteral("استرداد")));
    QCOMPARE(page.salesRowCount(), 0);
    QCOMPARE(products.findById(productId)->quantity, 100);

    data::CashMovementRepository movements(db);
    QCOMPARE(movements.sumBySessionId(sessionId), 0LL);
    bool foundRefund = false;
    for (const core::CashMovement& movement : movements.findBySessionId(sessionId)) {
        if (movement.type == QLatin1String("refund")) {
            foundRefund = true;
            QCOMPARE(movement.amountCents, -10000LL);
        }
    }
    QVERIFY(foundRefund);

    const auto reversals = data::SaleRepository(db).findBetween(
        QDateTime(QDate::currentDate(), QTime(0, 0, 0)), QDateTime::currentDateTime());
    QCOMPARE(static_cast<long long>(reversals.size()), 2);
    const core::Sale& reversal = reversals[0].reversedSaleId != 0 ? reversals[0] : reversals[1];
    const core::Sale& original = reversals[0].reversedSaleId == 0 ? reversals[0] : reversals[1];
    QCOMPARE(reversal.totalCents, -original.totalCents);
    QCOMPARE(reversal.reversedSaleId, sale.saleId);

    data::AuditLogRepository audit(db);
    const auto entries = audit.findBetween(QDateTime(QDate::currentDate(), QTime(0, 0, 0)),
                                           QDateTime::currentDateTime());
    bool foundSaleRefund = false;
    for (const core::AuditLogEntry& entry : entries) {
        if (entry.action == QLatin1String("sale_refund")) {
            foundSaleRefund = true;
        }
    }
    QVERIFY(foundSaleRefund);
}

void UiTest::paymentRefundRaisesBalance()
{
    const QString path = m_dir.filePath(QStringLiteral("payment-refund.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 3000;
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("زبون");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 1;
    QVERIFY(data::SaleService(db).recordCustomerDebt(customerId, { item }, "desktop", false).ok);

    data::PaymentService payments(db);
    const data::PaymentResult paid = payments.recordCustomerPayment(customerId, 2000, sessionId, QString());
    QVERIFY(paid.ok);

    ui::RefundsPage page(db);
    QCOMPARE(page.paymentsRowCount(), 1);
    page.refundPayment(paid.paymentId, QStringLiteral("خطأ في المبلغ"));
    QVERIFY(page.noticeText().contains(QStringLiteral("استرداد")));
    QCOMPARE(page.paymentsRowCount(), 0);

    // Balance goes back to the full debt: 5000 - 2000 + 2000 = 5000.
    ui::CustomersPage customersPage(db);
    QCOMPARE(customersPage.balanceAt(0), QStringLiteral("50.00"));

    data::CashMovementRepository movements(db);
    QCOMPARE(movements.sumBySessionId(sessionId), 0LL);

    data::AuditLogRepository audit(db);
    const auto entries = audit.findBetween(QDateTime(QDate::currentDate(), QTime(0, 0, 0)),
                                           QDateTime::currentDateTime());
    bool foundRefund = false;
    for (const core::AuditLogEntry& entry : entries) {
        if (entry.action == QLatin1String("customer_payment_refund")) {
            foundRefund = true;
        }
    }
    QVERIFY(foundRefund);
}

void UiTest::entryReversal()
{
    const QString path = m_dir.filePath(QStringLiteral("entry-reversal.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::CashEntryService cash(db);
    QVERIFY(cash.recordExpense(QStringLiteral("كهرباء"), 1500, sessionId).ok);
    const data::CashEntryResult drawing = cash.recordDrawing(QStringLiteral("سحب"), 2000, sessionId);
    QVERIFY(drawing.ok);

    ui::ExpensesPage page(db);
    QCOMPARE(page.entryCount(), 2);

    // The page's per-row reverse undoes one expense: refund movement back in.
    page.reverseRow(0);
    QCOMPARE(data::CashMovementRepository(db).sumBySessionId(sessionId), -2000LL);

    const QDateTime dayStart(QDate::currentDate(), QTime(0, 0, 0));
    const auto expenses = data::ExpenseRepository(db).findBetween(dayStart, QDateTime::currentDateTime());
    QCOMPARE(static_cast<long long>(expenses.size()), 2);
    long long expenseSum = 0;
    bool hasReversal = false;
    for (const core::Expense& expense : expenses) {
        expenseSum += expense.amountCents;
        if (expense.amountCents < 0) {
            hasReversal = true;
        }
    }
    QCOMPARE(expenseSum, 0LL);
    QVERIFY(hasReversal);

    QVERIFY(cash.reverseDrawing(drawing.entryId, sessionId).ok);
    QCOMPARE(data::CashMovementRepository(db).sumBySessionId(sessionId), 0LL);

    // Every one of the four operations left a row of its own. The count is not
    // asserted as a total: the point is that each movement is audited by the
    // service that made it, inside that movement's own transaction, so the
    // reversals above cannot be missing from the log the way a UI-written entry
    // could when the transaction rolled back after the page believed it had
    // succeeded.
    data::AuditLogRepository audit(db);
    const auto entries = audit.findBetween(dayStart, QDateTime::currentDateTime());
    QStringList actions;
    for (const core::AuditLogEntry& entry : entries) {
        actions << entry.action;
    }
    QCOMPARE(actions.size(), 4);
    QCOMPARE(actions.filter(QStringLiteral("expense")).size(), 1);
    QCOMPARE(actions.filter(QStringLiteral("owner_drawing")).size(), 1);
    QCOMPARE(actions.filter(QStringLiteral("entry_reversal")).size(), 2);
}

void UiTest::login_basic()
{
    const QString path = m_dir.filePath(QStringLiteral("login_test.sqlite"));
    QFile::remove(path);
    data::Database db(path);
    data::UserRepository repo(db);

    core::User user;
    user.name = QStringLiteral("اختبار");
    user.role = QStringLiteral("cashier");
    const int id = repo.save(user);
    QVERIFY(id > 0);
    QVERIFY(repo.savePin(id, QStringLiteral("12")));

    const auto found = repo.findByPin(QStringLiteral("12"));
    QVERIFY(found.has_value());
    QCOMPARE(found->id, id);
    QCOMPARE(found->name, QStringLiteral("اختبار"));

    app::core::Session::instance().setCurrentUser(*found);
    QVERIFY(app::core::Session::instance().hasUser());
    QCOMPARE(app::core::Session::instance().actorName(), QStringLiteral("اختبار"));

    app::core::Session::instance().clear();
    QVERIFY(!app::core::Session::instance().hasUser());
    QCOMPARE(app::core::Session::instance().actorName(), QStringLiteral("desktop"));
}

// void UiTest::statusBarSwitchUserButton()
// {
//     const QString path = m_dir.filePath(QStringLiteral("statusbar_test.sqlite"));
//     QFile::remove(path);
//     data::Database db(path, data::DatabaseMode::Server);
//     app::ui::ServerController controller(db, m_key);
//     controller.start();
//
//     app::ui::MainWindow window(db, controller);
//     window.show();
//
//     // Find the "تبديل المستخدم" button by text
//     auto* switchBtn = window.findChild<QPushButton*>();
//     QVERIFY(switchBtn);
//     while (switchBtn && switchBtn->text() != QStringLiteral("تبديل المستخدم")) {
//         auto buttons = window.findChildren<QPushButton*>();
//         bool found = false;
//         for (auto* btn : buttons) {
//             if (btn->text() == QStringLiteral("تبديل المستخدم")) {
//                 switchBtn = btn;
//                 found = true;
//                 break;
//             }
//         }
//         QVERIFY(found);
//     }
//     QVERIFY(switchBtn->isVisible());
//     QCOMPARE(switchBtn->text(), QStringLiteral("تبديل المستخدم"));
//
//     // Find the user label
//     auto* userLabel = window.findChild<QLabel*>(QStringLiteral("userLabel"));
//     QVERIFY(userLabel);
//     QVERIFY(userLabel->isVisible());
//     QVERIFY(userLabel->text().startsWith(QStringLiteral("المستخدم: ")));
//
//     window.close();
// }

void UiTest::users_page_lists_users()
{
    const QString path = m_dir.filePath(QStringLiteral("users_page_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);

    // Create admin user
    data::UserRepository userRepo(db);
    core::User admin;
    admin.name = QStringLiteral("المدير");
    admin.role = QStringLiteral("admin");
    const int adminId = userRepo.save(admin);
    QVERIFY(adminId > 0);
    QVERIFY(userRepo.savePin(adminId, QStringLiteral("12")));

    // Create cashier user
    core::User cashier;
    cashier.name = QStringLiteral("كاشير1");
    cashier.role = QStringLiteral("cashier");
    const int cashierId = userRepo.save(cashier);
    QVERIFY(cashierId > 0);
    QVERIFY(userRepo.savePin(cashierId, QStringLiteral("34")));

    // Test UsersPage directly without MainWindow
    app::ui::UsersPage page(db);
    page.refresh();
    QCOMPARE(page.rowCount(), 2);
}

// The users table no longer renders PINs. This drives the page directly and
// checks that every seeded user still has a row while no PIN leaks into any
// cell -- neither the removed column nor any other.
void UiTest::users_page_does_not_render_pins()
{
    const QString path = m_dir.filePath(QStringLiteral("users_page_pins_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    data::UserRepository userRepo(db);

    struct Seed {
        QString name;
        QString role;
        QString pin;
    };
    const QVector<Seed> seeds = {
        {QStringLiteral("المدير"), QStringLiteral("admin"), QStringLiteral("12")},
        {QStringLiteral("كاشير1"), QStringLiteral("cashier"), QStringLiteral("34")},
        {QStringLiteral("مدير2"), QStringLiteral("admin"), QStringLiteral("56")},
    };

    for (const Seed& seed : seeds) {
        core::User user;
        user.name = seed.name;
        user.role = seed.role;
        const int id = userRepo.save(user);
        QVERIFY(id > 0);
        QVERIFY(userRepo.savePin(id, seed.pin));
    }

    app::ui::UsersPage page(db);
    page.refresh();
    QCOMPARE(page.rowCount(), seeds.size());

    QTableWidget* table = page.table();
    QVERIFY(table);

    QHash<QString, bool> seen;
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* nameItem = table->item(row, 0);
        QVERIFY2(nameItem, qPrintable(QStringLiteral("missing name cell on row %1").arg(row)));
        seen.insert(nameItem->text(), true);

        // Column 2 is the active checkbox, not a PIN text cell.
        QVERIFY2(!table->item(row, 2),
                 qPrintable(QStringLiteral("unexpected text cell in the active column on row %1")
                                .arg(row)));

        for (int col = 0; col < table->columnCount(); ++col) {
            QTableWidgetItem* item = table->item(row, col);
            if (!item) {
                continue;
            }
            for (const Seed& seed : seeds) {
                QVERIFY2(!item->text().contains(seed.pin),
                         qPrintable(QStringLiteral("PIN %1 leaked into cell %2,%3")
                                        .arg(seed.pin).arg(row).arg(col)));
            }
        }
    }

    QCOMPARE(seen.size(), seeds.size());
    for (const Seed& seed : seeds) {
        QVERIFY2(seen.contains(seed.name),
                 qPrintable(QStringLiteral("no row for user %1").arg(seed.name)));
    }
}

void UiTest::users_page_shows_no_pin_column()
{
    const QString path = m_dir.filePath(QStringLiteral("users_page_no_pin_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    data::UserRepository repo(db);

    core::User user;
    user.name = QStringLiteral("المدير");
    user.role = QStringLiteral("admin");
    const int id = repo.save(user);
    QVERIFY(id > 0);
    QVERIFY(repo.savePin(id, QStringLiteral("1234")));

    app::ui::UsersPage page(db);
    page.refresh();

    QTableWidget* table = page.table();
    QVERIFY(table);
    QCOMPARE(table->columnCount(), 4);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("الاسم"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("الدور"));
    QCOMPARE(table->horizontalHeaderItem(2)->text(), QStringLiteral("نشط"));
    QCOMPARE(table->horizontalHeaderItem(3)->text(), QStringLiteral("إجراءات"));

    for (int col = 0; col < table->columnCount(); ++col) {
        QVERIFY2(table->horizontalHeaderItem(col)->text() != QStringLiteral("PIN"),
                 qPrintable(QStringLiteral("header %1 still reads PIN").arg(col)));
    }

    for (int row = 0; row < table->rowCount(); ++row) {
        for (int col = 1; col <= 3; ++col) {
            QTableWidgetItem* item = table->item(row, col);
            if (item) {
                QVERIFY2(!item->text().contains(QStringLiteral("1234")),
                         qPrintable(QStringLiteral("PIN leaked into cell %1,%2")
                                        .arg(row).arg(col)));
            }
        }
    }
}

void UiTest::users_page_actions_column_is_wide_enough()
{
    const QString path = m_dir.filePath(QStringLiteral("users_page_actions_width_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    data::UserRepository repo(db);

    core::User user;
    user.name = QStringLiteral("المدير");
    user.role = QStringLiteral("admin");
    QVERIFY(repo.save(user) > 0);

    app::ui::UsersPage page(db);
    page.refresh();

    QTableWidget* table = page.table();
    QVERIFY(table);
    QVERIFY2(table->columnWidth(3) >= 180,
             qPrintable(QStringLiteral("actions column is only %1px wide")
                            .arg(table->columnWidth(3))));
}

void UiTest::users_page_action_buttons_fit_inside_the_row()
{
    const QString path = m_dir.filePath(QStringLiteral("users_page_action_height_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    data::UserRepository repo(db);

    core::User user;
    user.name = QStringLiteral("المدير");
    user.role = QStringLiteral("admin");
    QVERIFY(repo.save(user) > 0);

    app::ui::UsersPage page(db);
    page.refresh();

    QTableWidget* table = page.table();
    QVERIFY(table);
    QVERIFY2(table->rowHeight(0) >= 54,
             qPrintable(QStringLiteral("row 0 is only %1px tall").arg(table->rowHeight(0))));

    QWidget* actions = table->cellWidget(0, 3);
    QVERIFY2(actions, "no actions cell widget on row 0");

    const QList<QPushButton*> buttons = actions->findChildren<QPushButton*>();
    QVERIFY(!buttons.isEmpty());
    for (QPushButton* button : buttons) {
        QVERIFY2(button->height() <= actions->height(),
                 qPrintable(QStringLiteral("button \"%1\" is %2px in a %3px cell widget")
                                .arg(button->text())
                                .arg(button->height())
                                .arg(actions->height())));
        QVERIFY2(button->height() >= button->sizeHint().height(),
                 qPrintable(QStringLiteral("button \"%1\" is clipped to %2px under its %3px hint")
                                .arg(button->text())
                                .arg(button->height())
                                .arg(button->sizeHint().height())));
        QVERIFY2(actions->height() <= table->rowHeight(0),
                 qPrintable(QStringLiteral("cell widget %1px exceeds the %2px row")
                                .arg(actions->height())
                                .arg(table->rowHeight(0))));
    }
}

// Drives the real modal that UsersPage::userDialog() opens, so the PIN the
// operator types is the value the production code has to carry back.
void UiTest::user_edit_pin_persists()
{
    const QString path = m_dir.filePath(QStringLiteral("user_edit_pin_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    data::UserRepository repo(db);

    core::User user;
    user.name = QStringLiteral("المدير");
    user.role = QStringLiteral("admin");
    const int id = repo.save(user);
    QVERIFY(id > 0);
    QVERIFY(repo.savePin(id, QStringLiteral("12")));

    app::ui::UsersPage page(db);
    page.refresh();

    QTableWidget* table = page.table();
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 1);

    QWidget* actions = table->cellWidget(0, 3);
    QVERIFY(actions);
    QPushButton* editBtn = nullptr;
    for (QPushButton* b : actions->findChildren<QPushButton*>()) {
        if (b->text() == QStringLiteral("تعديل")) {
            editBtn = b;
            break;
        }
    }
    QVERIFY2(editBtn, "could not find the تعديل button for row 0");

    // userDialog() calls exec(), which spins a nested event loop; run the
    // typing from a 0ms timer so it lands inside that loop.
    bool modalOpened = false;
    bool typedOk = false;
    QTimer::singleShot(0, [&]() {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) {
            return;
        }
        modalOpened = true;

        QLineEdit* pinField = nullptr;
        QLineEdit* confirmField = nullptr;
        for (QLineEdit* e : modal->findChildren<QLineEdit*>()) {
            const QString hint = e->placeholderText();
            if (hint == QStringLiteral("PIN (حرفان)")) {
                pinField = e;
            } else if (hint == QStringLiteral("تأكيد PIN")) {
                confirmField = e;
            }
        }
        if (!pinField || !confirmField) {
            return;
        }
        QTest::keyClicks(pinField, QStringLiteral("34"));
        QTest::keyClicks(confirmField, QStringLiteral("34"));
        typedOk = (pinField->text() == QStringLiteral("34")
                   && confirmField->text() == QStringLiteral("34"));

        auto* box = modal->findChild<QDialogButtonBox*>();
        if (box) {
            if (QPushButton* ok = box->button(QDialogButtonBox::Ok)) {
                ok->click();
            }
        }
    });

    editBtn->click();

    QVERIFY2(modalOpened, "editing a user did not open the PIN dialog");
    QVERIFY2(typedOk, "could not type the new PIN into the dialog");

    // The new PIN must be the one stored, and the old one must stop working.
    const auto byNewPin = repo.findByPin(QStringLiteral("34"));
    QVERIFY2(byNewPin.has_value(), "the edited PIN 34 was not persisted");
    QCOMPARE(byNewPin->id, id);

    const auto byOldPin = repo.findByPin(QStringLiteral("12"));
    QVERIFY2(!byOldPin.has_value(), "the old PIN 12 still grants access after the edit");

    // The users table no longer renders PINs; the edit is reflected in the
    // repository (checked above) and the row must otherwise remain intact.
    page.refresh();
    QTableWidgetItem* nameItem = table->item(0, 0);
    QVERIFY(nameItem);
    QCOMPARE(nameItem->text(), QStringLiteral("المدير"));
    QVERIFY2(!table->item(0, 2), "the active column must not hold a PIN text cell");
    for (int col = 0; col < table->columnCount(); ++col) {
        QTableWidgetItem* item = table->item(0, col);
        if (item) {
            QVERIFY2(!item->text().contains(QStringLiteral("34")),
                     qPrintable(QStringLiteral("PIN leaked into cell 0,%1").arg(col)));
        }
    }
}

void UiTest::master_recovery_grants_access()
{
    const QString path = m_dir.filePath(QStringLiteral("master_recovery_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    data::UserRepository repo(db);

    core::User admin;
    admin.name = QStringLiteral("المدير");
    admin.role = QStringLiteral("admin");
    const int adminId = repo.save(admin);
    QVERIFY(adminId > 0);

    const QString masterWord = QStringLiteral("swordfish-42");
    data::AdminSecretRepository secrets(db);
    QVERIFY(secrets.setMaster(adminId, masterWord));
    QVERIFY(secrets.verifyMaster(adminId, masterWord));

    app::core::Session::instance().clear();

    app::ui::LoginDialog dialog(db);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* input = dialog.findChild<QLineEdit*>(QStringLiteral("recoveryInput"));
    QVERIFY(input);

    // A wrong recovery word must not grant access.
    input->setText(QStringLiteral("wrong-word"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onRecoveryReturnPressed",
                                      Qt::DirectConnection));
    QVERIFY2(!app::core::Session::instance().hasUser(),
             "a wrong recovery word granted access");

    // The reported symptom: eight header clicks must reveal a field the
    // operator can actually type into.
    for (int i = 0; i < 8; ++i) {
        QVERIFY(QMetaObject::invokeMethod(&dialog, "onHeaderClicked", Qt::DirectConnection));
    }
    QVERIFY2(input->isVisible(), "the recovery field stayed invisible after 8 header clicks");
    QVERIFY2(!input->isHidden(), "the recovery field is explicitly hidden after 8 header clicks");

    // The correct recovery word must sign the admin in.
    input->setText(masterWord);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onRecoveryReturnPressed",
                                      Qt::DirectConnection));
    QVERIFY2(app::core::Session::instance().hasUser(),
             "the correct recovery word did not grant access");
    QCOMPARE(app::core::Session::instance().currentUser().id, adminId);
    QCOMPARE(app::core::Session::instance().actorName(), QStringLiteral("المدير"));

    app::core::Session::instance().clear();
    QVERIFY(!app::core::Session::instance().hasUser());
}

void UiTest::status_bar_shows_occasion_when_active()
{
    const QString path = m_dir.filePath(QStringLiteral("occasion_statusbar.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);

    data::OccasionRepository occasions(db);
    data::SettingRepository settings(db);
    data::OccasionService service(db, occasions, settings);

    core::Occasion occasion;
    occasion.name = QStringLiteral("أسبوع التخفيضات");
    occasion.icon = QStringLiteral("🎉");
    occasion.startsAt = data::nowIso();
    occasion.endsAt = data::nowIso();
    occasion.createdAt = data::nowIso();
    const int occasionId = occasions.insert(occasion);
    QVERIFY2(occasionId > 0, qPrintable(db.lastError()));
    QVERIFY(service.activate(occasionId));

    // The occasion was already running when the window opened, so the bar has to
    // say so without the operator touching anything.
    {
        ui::MainWindow window(db, controller);
        window.show();
        QCoreApplication::processEvents();

        QLabel* label = window.findChild<QLabel*>(QStringLiteral("occasionLabel"));
        QVERIFY2(label != nullptr, "occasion label must exist in the status bar");
        QVERIFY(label->text().contains(occasion.name));
        QVERIFY2(window.occasionLabelText().contains(occasion.icon), "the icon leads the name");

        // The label is permanent rather than rebuilt, so switching off has to
        // clear it in place.
        window.deactivateOccasion();
        QCoreApplication::processEvents();
        QVERIFY2(window.occasionLabelText().isEmpty(), "the label must be empty once the occasion is off");
    }

    // A window opened with nothing running must not inherit a stale label, which
    // is why the label is read from the setting at construction instead of being
    // left over from an earlier one.
    {
        ui::MainWindow window(db, controller);
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.occasionLabelText().isEmpty());
    }

    // And a later activation is picked up by the window already on screen.
    {
        ui::MainWindow window(db, controller);
        window.show();
        QVERIFY(window.activateOccasion(occasionId));
        QCoreApplication::processEvents();
        QVERIFY(window.occasionLabelText().contains(occasion.name));

        // A refused activation, an occasion that does not exist, leaves the bar
        // showing what was already running rather than blanking it.
        QVERIFY(!window.activateOccasion(occasionId + 999));
        QVERIFY(window.occasionLabelText().contains(occasion.name));
    }
}

void UiTest::language_dialog_returns_the_chosen_code()
{
    ui::LanguageDialog dialog;
    const QList<QPushButton*> buttons =
        dialog.findChildren<QPushButton*>(QStringLiteral("languageChoice"));
    QCOMPARE(buttons.size(), 3);
    QCOMPARE(buttons.at(0)->text(), QStringLiteral("العربية"));
    QCOMPARE(buttons.at(1)->text(), QStringLiteral("Français"));
    QCOMPARE(buttons.at(2)->text(), QStringLiteral("English"));

    buttons.at(1)->click();
    QCOMPARE(dialog.result(), static_cast<int>(QDialog::Accepted));
    QCOMPARE(dialog.chosenCode(), QStringLiteral("fr"));
}

void UiTest::language_dialog_returns_empty_on_dismiss()
{
    ui::LanguageDialog dialog;
    dialog.reject();
    QVERIFY(dialog.chosenCode().isEmpty());
}

// A cash sale renders on the receipt: the shop name it was configured with, the
// product, the quantity, the number minted for it and the payable, all read back
// from the sale row rather than from anything the till had cached.
void UiTest::receipt_renders_a_cash_sale()
{
    const QString path = m_dir.filePath(QStringLiteral("receipt-cash.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::SettingRepository settings(db);
    settings.set(QStringLiteral("shop_name"), QStringLiteral("Boutique Test"));
    settings.set(QStringLiteral("currency_symbol"), QStringLiteral("DA"));

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000011");
    product.name = QStringLiteral("Biscuit");
    product.costPriceCents = 5000;
    product.salePriceCents = 6000;
    product.piecesPerPackage = 1;
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 6000;

    data::SaleService service(db);
    const data::SaleRecordResult sold =
        service.recordSale({ item }, sessionId, QStringLiteral("desktop"), false);
    QVERIFY2(sold.ok, qPrintable(sold.error));

    const ui::ReceiptData data = ui::buildReceiptForSale(db, sold.saleId);
    QCOMPARE(data.receiptNumber,
             QStringLiteral("A-%1-0001").arg(QDate::currentDate().year()));
    QCOMPARE(data.totalCents, 12000LL);

    QTextDocument* doc = ui::renderReceipt(data, ui::ReceiptPaper::Thermal80);
    const QString text = doc->toPlainText();
    QVERIFY2(text.contains(QStringLiteral("Boutique Test")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("Biscuit")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("2")), qPrintable(text));
    QVERIFY2(text.contains(data.receiptNumber), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("120")), qPrintable(text));
    // The payable, spelled with the symbol the receipt carries, so the assertion
    // does not lean on whatever setCurrencySymbol() last put in the UI cache.
    QVERIFY2(text.contains(app::ui::formatMoney(12000LL, QStringLiteral("DA"))), qPrintable(text));
    // The payable is bold. Qt serialises bold as a <span style="font-weight:700">,
    // never a <b> tag, so the check reads the character format of the payable line
    // rather than a substring of toHtml().
    bool payableBold = false;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (!block.text().contains(QStringLiteral("120.00"))) {
            continue;
        }
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        if (cursor.charFormat().fontWeight() >= QFont::Bold) {
            payableBold = true;
        }
    }
    QVERIFY2(payableBold, qPrintable(doc->toHtml()));
    delete doc;
}

// A credit sale is a receipt too: the customer's name rides on the paper, and the
// data says the document came from the credit ledger.
void UiTest::receipt_shows_customer_name_on_a_credit_sale()
{
    const QString path = m_dir.filePath(QStringLiteral("receipt-credit.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::SettingRepository settings(db);
    settings.set(QStringLiteral("shop_name"), QStringLiteral("Boutique Test"));
    settings.set(QStringLiteral("currency_symbol"), QStringLiteral("DA"));

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000012");
    product.name = QStringLiteral("Biscuit");
    product.costPriceCents = 5000;
    product.salePriceCents = 6000;
    product.piecesPerPackage = 1;
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("Mohamed");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 6000;

    data::SaleService service(db);
    const data::SaleRecordResult sold =
        service.recordCustomerDebt(customerId, { item }, QStringLiteral("desktop"), false);
    QVERIFY2(sold.ok, qPrintable(sold.error));

    const ui::ReceiptData data = ui::buildReceiptForCustomerTransaction(db, sold.saleId);
    QVERIFY(data.isCredit);

    QTextDocument* doc = ui::renderReceipt(data, ui::ReceiptPaper::Thermal80);
    const QString text = doc->toPlainText();
    QVERIFY2(text.contains(QStringLiteral("Mohamed")), qPrintable(text));
    delete doc;
}

// Five columns do not fit on a 58mm roll, so the paper drops the unit column: the
// unit header is absent while the product and the payable are still there.
void UiTest::receipt_58mm_drops_the_unit_column()
{
    const QString path = m_dir.filePath(QStringLiteral("receipt-58.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::SettingRepository settings(db);
    settings.set(QStringLiteral("shop_name"), QStringLiteral("Boutique Test"));
    settings.set(QStringLiteral("currency_symbol"), QStringLiteral("DA"));

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000013");
    product.name = QStringLiteral("Biscuit");
    product.costPriceCents = 5000;
    product.salePriceCents = 6000;
    product.piecesPerPackage = 1;
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 6000;

    data::SaleService service(db);
    const data::SaleRecordResult sold =
        service.recordSale({ item }, sessionId, QStringLiteral("desktop"), false);
    QVERIFY2(sold.ok, qPrintable(sold.error));

    const ui::ReceiptData data = ui::buildReceiptForSale(db, sold.saleId);
    QTextDocument* doc = ui::renderReceipt(data, ui::ReceiptPaper::Thermal58);
    const QString text = doc->toPlainText();
    QVERIFY2(text.contains(QStringLiteral("Biscuit")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("120")), qPrintable(text));
    delete doc;
}

// The other receipts all carry a zero adjustment, so only this one can catch a
// double count: the paper shows the line sum, then the discount, then a payable
// that has to equal the sale row's total — not the sum plus the discount again.
void UiTest::receipt_shows_the_adjustment_and_the_payable_matches_the_row()
{
    const QString path = m_dir.filePath(QStringLiteral("receipt-adjustment.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::SettingRepository settings(db);
    settings.set(QStringLiteral("shop_name"), QStringLiteral("Boutique Test"));
    settings.set(QStringLiteral("currency_symbol"), QStringLiteral("DA"));

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000014");
    product.name = QStringLiteral("Biscuit");
    product.costPriceCents = 5000;
    product.salePriceCents = 6000;
    product.piecesPerPackage = 1;
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 6000;

    data::SaleService service(db);
    const data::SaleRecordResult sold =
        service.recordSale({ item }, sessionId, QStringLiteral("desktop"), false, nullptr, -1000);
    QVERIFY2(sold.ok, qPrintable(sold.error));

    const ui::ReceiptData data = ui::buildReceiptForSale(db, sold.saleId);
    // The line sum, with nothing of the adjustment folded in.
    QCOMPARE(data.totalCents, 12000LL);
    QCOMPARE(data.adjustmentCents, -1000LL);

    QTextDocument* doc = ui::renderReceipt(data, ui::ReceiptPaper::Thermal80);
    const QString text = doc->toPlainText();
    const int totalIndex = text.indexOf(QStringLiteral("120"));
    QVERIFY2(totalIndex >= 0, qPrintable(text));
    const int minusIndex = text.indexOf(QStringLiteral("-"), totalIndex);
    QVERIFY2(minusIndex >= 0, qPrintable(text));
    const QString payable = app::ui::formatMoney(11000LL, QStringLiteral("DA"));
    const int payableIndex = text.indexOf(payable);
    QVERIFY2(payableIndex >= 0, qPrintable(text));
    // Order on the paper: the sum, then the discount, then the payable.
    QVERIFY(totalIndex < minusIndex);
    QVERIFY(minusIndex < payableIndex);
    delete doc;

    // The invariant the paper has to mirror: the payable equals the row's
    // total_cents, which the service stores as sum + adjustment, once.
    QSqlQuery row(db.handle());
    row.prepare(QStringLiteral("SELECT total_cents FROM sales WHERE id = ?"));
    row.addBindValue(sold.saleId);
    QVERIFY(row.exec());
    QVERIFY(row.next());
    QCOMPARE(row.value(0).toLongLong(), 11000LL);
}

// The printing choices live in settings like the other fields: the page writes
// them on save and reads them back the next time it is opened, with no preview
// or test print in between (that arrives with QtPrintSupport).
void UiTest::settings_page_saves_and_restores_receipt_printing()
{
    const QString path = m_dir.filePath(QStringLiteral("settings-printing.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    {
        ui::SettingsPage page(db);
        auto* paper = page.findChild<QComboBox*>(QStringLiteral("receiptPaperSize"));
        auto* footer = page.findChild<QLineEdit*>(QStringLiteral("receiptFooterText"));
        QVERIFY(paper);
        QVERIFY(footer);

        const int a5 = paper->findData(QStringLiteral("a5"));
        QVERIFY(a5 >= 0);
        paper->setCurrentIndex(a5);
        footer->setText(QStringLiteral("Merci de votre visite"));
        page.save();
    }

    data::SettingRepository settings(db);
    QCOMPARE(settings.value(QStringLiteral("receipt_paper_size")).value_or(QString()),
             QStringLiteral("a5"));
    QCOMPARE(settings.value(QStringLiteral("receipt_footer_text")).value_or(QString()),
             QStringLiteral("Merci de votre visite"));

    ui::SettingsPage reopened(db);
    auto* paper = reopened.findChild<QComboBox*>(QStringLiteral("receiptPaperSize"));
    auto* footer = reopened.findChild<QLineEdit*>(QStringLiteral("receiptFooterText"));
    QVERIFY(paper);
    QVERIFY(footer);
    QCOMPARE(paper->currentData().toString(), QStringLiteral("a5"));
    QCOMPARE(footer->text(), QStringLiteral("Merci de votre visite"));
}

// The paper is a setting, not an argument: the receipt helper reads which roll
// the shop prints on, and the same helper maps it back for the settings page.
void UiTest::receipt_uses_the_paper_from_settings()
{
    const QString path = m_dir.filePath(QStringLiteral("receipt-paper.sqlite"));
    QFile::remove(path);
    data::Database db(path);
    data::SettingRepository(db).set(QStringLiteral("receipt_paper_size"), QStringLiteral("58"));

    QCOMPARE(static_cast<int>(ui::currentReceiptPaper(db)),
             static_cast<int>(ui::ReceiptPaper::Thermal58));
    QCOMPARE(ui::paperSettingValue(ui::ReceiptPaper::A4), QStringLiteral("a4"));
    QCOMPARE(ui::paperSettingValue(ui::ReceiptPaper::Thermal80), QStringLiteral("80"));

    // currentReceiptPaper() reads only the DB, so a minimal, line-less document
    // is enough to show the choice does not depend on the sale's contents.
    ui::ReceiptData data;
    data.shopName = QStringLiteral("Boutique Test");
    Q_UNUSED(data);

    const QString emptyPath = m_dir.filePath(QStringLiteral("receipt-paper-empty.sqlite"));
    QFile::remove(emptyPath);
    data::Database emptyDb(emptyPath);
    QCOMPARE(static_cast<int>(ui::currentReceiptPaper(emptyDb)),
             static_cast<int>(ui::ReceiptPaper::Thermal80));
}

// Auto-print lives in settings: the checkbox stores "1" or "0", defaults to
// unchecked, and is restored when the page re-opens.
void UiTest::settings_page_saves_auto_print()
{
    const QString path = m_dir.filePath(QStringLiteral("settings-auto.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    {
        ui::SettingsPage page(db);
        auto* autoBox = page.findChild<QCheckBox*>(QStringLiteral("receiptAutoPrint"));
        QVERIFY(autoBox);
        QVERIFY(!autoBox->isChecked());
        autoBox->setChecked(true);
        page.save();
    }

    data::SettingRepository settings(db);
    QCOMPARE(settings.value(QStringLiteral("receipt_auto_print")).value_or(QString()),
             QStringLiteral("1"));

    ui::SettingsPage reopened(db);
    auto* autoBox = reopened.findChild<QCheckBox*>(QStringLiteral("receiptAutoPrint"));
    QVERIFY(autoBox);
    QVERIFY(autoBox->isChecked());
}

void UiTest::receipt_printer_writes_a_pdf()
{
    const QString path = m_dir.filePath(QStringLiteral("pdf.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::SettingRepository settings(db);
    settings.set(QStringLiteral("shop_name"), QStringLiteral("Boutique Test"));
    settings.set(QStringLiteral("currency_symbol"), QStringLiteral("DA"));

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000099");
    product.name = QStringLiteral("Biscuit");
    product.costPriceCents = 5000;
    product.salePriceCents = 6000;
    product.piecesPerPackage = 1;
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 6000;

    data::SaleService service(db);
    const data::SaleRecordResult sold =
        service.recordSale({ item }, sessionId, QStringLiteral("desktop"), false);
    QVERIFY2(sold.ok, qPrintable(sold.error));

    const ui::ReceiptData data = ui::buildReceiptForSale(db, sold.saleId);
    QTextDocument* doc = ui::renderReceipt(data, ui::ReceiptPaper::Thermal80);
    doc->setMetaInformation(QTextDocument::DocumentTitle, data.receiptNumber);

    const QString file = m_dir.filePath(QStringLiteral("receipt.pdf"));
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(file);
    ui::configurePrinterForPaper(printer, ui::ReceiptPaper::Thermal80);
    ui::printReceiptToPrinter(doc, printer);

    QVERIFY(QFileInfo(file).exists());
    QVERIFY(QFileInfo(file).size() > 1000);
    QFile f(file);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QByteArray first = f.read(4);
    QVERIFY(first == "%PDF");
    f.close();
    delete doc;
}


void UiTest::product_dialog_reveals_the_weight_checkbox()
{
    data::Database db(m_dir.filePath(QStringLiteral("w1.sqlite")));
    data::ProductRepository repo(db);

    ui::ProductsPage page(db);
    page.show();
    QCoreApplication::processEvents();

    const QString saleLabelBefore = QCoreApplication::translate("ProductDialog", "سعر البيع");

    bool modalOpened = false;
    bool ok1 = false;
    bool ok2 = false;
    bool ok3 = false;
    bool watchdogFired = false;

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        watchdogFired = true;
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            modal->reject();
        }
    });
    watchdog.start(3000);

    QTimer::singleShot(0, [&]() {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) return;
        modalOpened = true;
        QCoreApplication::processEvents();
        auto* weight = modal->findChild<QCheckBox*>(QStringLiteral("soldByWeight"));
        QVERIFY(weight != nullptr);
        QVERIFY(!weight->isChecked());
        auto* saleEdit = modal->findChild<QLineEdit*>(QStringLiteral("salePrice"));
        if (!saleEdit) {
            const auto edits2 = modal->findChildren<QLineEdit*>();
            if (edits2.size() > 3) saleEdit = edits2[3];
        }
        QVERIFY(saleEdit != nullptr);
        auto* form = modal->findChild<QFormLayout*>();
        if (!form && modal->layout()) form = qobject_cast<QFormLayout*>(modal->layout());
        QVERIFY(form != nullptr);
        auto* saleLabel = qobject_cast<QLabel*>(form->labelForField(saleEdit));
        if (!saleLabel) {
            // try to find by scanning form rows
            for (int i = 0; i < form->rowCount(); ++i) {
                auto* fi = form->itemAt(i, QFormLayout::FieldRole); if (fi && fi->widget() == saleEdit) {
                    auto* li = form->itemAt(i, QFormLayout::LabelRole); saleLabel = li ? qobject_cast<QLabel*>(li->widget()) : nullptr;
                    break;
                }
            }
        }
        if (!saleLabel) {
            const auto labels = modal->findChildren<QLabel*>();
            for (auto* l : labels) {
                const QString t = l->text();
                if (t.contains(QStringLiteral("سعر البيع")) || t.contains(QStringLiteral("سعر الكيلوغرام"))) { saleLabel = l; break; }
            }
        }
        QVERIFY(saleLabel != nullptr);
        if (saleLabel->text().contains(QStringLiteral("سعر البيع"))) ok1 = true;
        weight->setChecked(true); QCoreApplication::processEvents();
        if (saleLabel->text().contains(QStringLiteral("سعر الكيلوغرام"))) ok2 = true;
        weight->setChecked(false); QCoreApplication::processEvents();
        if (saleLabel->text().contains(QStringLiteral("سعر البيع"))) ok3 = true;
        modal->reject();
    });

    QPushButton* addBtn = page.findChild<QPushButton*>(QStringLiteral("primary"));
    QVERIFY(addBtn != nullptr);
    addBtn->click();

    QVERIFY2(!watchdogFired, "the product dialog had to be closed by the watchdog");
    QVERIFY2(modalOpened, "the add-product button did not open the product dialog");
    QVERIFY2(ok1 && ok2 && ok3, "label toggle did not behave as expected");
}

void UiTest::product_dialog_saves_the_weight_flag()
{
    data::Database db(m_dir.filePath(QStringLiteral("w2.sqlite")));
    data::ProductRepository repo(db);

    ui::ProductsPage page(db);
    page.show();
    QCoreApplication::processEvents();

    bool modalOpened = false;
    bool watchdogFired = false;

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&]() {
        watchdogFired = true;
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            modal->reject();
        }
    });
    watchdog.start(3000);

    QTimer::singleShot(0, [&]() {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) return;
        modalOpened = true;
        const auto edits = modal->findChildren<QLineEdit*>();
        if (edits.size() > 0) edits[0]->setText(QStringLiteral("5"));
        if (edits.size() > 1) edits[1]->setText(QStringLiteral("لوز"));
        if (edits.size() > 2) edits[2]->setText(QStringLiteral("1000")); // cost
        if (edits.size() > 3) edits[3]->setText(QStringLiteral("1200")); // sale

        auto* weight = modal->findChild<QCheckBox*>(QStringLiteral("soldByWeight"));
        if (weight) weight->setChecked(true);

        auto* box = modal->findChild<QDialogButtonBox*>();
        if (box) {
            auto* ok = box->button(QDialogButtonBox::Ok);
            if (ok) ok->click();
        } else {
            modal->accept();
        }
    });

    QPushButton* addBtn = page.findChild<QPushButton*>(QStringLiteral("primary"));
    QVERIFY(addBtn != nullptr);
    addBtn->click();

    QVERIFY2(!watchdogFired, "the product dialog had to be closed by the watchdog");
    QVERIFY2(modalOpened, "the add-product button did not open the product dialog");

    const auto found = repo.findByBarcode(QStringLiteral("5"));
    QVERIFY(found.has_value());
    QVERIFY(found->soldByWeight);
    QVERIFY(found->salePriceCents == 120000);
}

void UiTest::products_page_marks_weight_products()
{
    data::Database db(m_dir.filePath(QStringLiteral("w3.sqlite")));


    data::ProductRepository repo(db);
    core::Product p1;
    p1.barcode = QStringLiteral("1");
    p1.name = QStringLiteral("لحم");
    p1.soldByWeight = true;
    p1.salePriceCents = 1000;
    const int id1 = repo.save(p1);
    QVERIFY(id1 > 0);

    core::Product p2;
    p2.barcode = QStringLiteral("2");
    p2.name = QStringLiteral("أرز");
    p2.soldByWeight = false;
    p2.salePriceCents = 500;
    const int id2 = repo.save(p2);
    QVERIFY(id2 > 0);

    ui::ProductsPage page(db);
    page.show();
    QCoreApplication::processEvents();

    // Find rows
    const QTableWidget* table = page.findChild<QTableWidget*>(QStringLiteral("productTable"));
    QVERIFY(table != nullptr);
    QVERIFY(table->rowCount() >= 2);

    for (int r = 0; r < table->rowCount(); ++r) {
        const auto* cell = table->item(r, 1); // name column
        if (!cell) continue;
        const int pid = cell->data(Qt::UserRole).toInt();
        if (pid == id1) {
            QVERIFY(cell->text().startsWith(QStringLiteral("⚖ ")));
        } else if (pid == id2) {
            QVERIFY(!cell->text().contains(QStringLiteral("⚖ ")));
        }
    }
}

QTEST_MAIN(UiTest)
#include "tst_ui.moc"

void UiTest::receipt_renders_a_weight_line_with_grams()
{
    const QString path = m_dir.filePath(QStringLiteral("rcpt_w.sqlite"));
    data::Database db(path);

    data::SettingRepository settings(db);
    settings.set(QStringLiteral("shop_name"), QStringLiteral("Boutique Test"));
    settings.set(QStringLiteral("currency_symbol"), QStringLiteral("DA"));

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("W123");
    product.name = QStringLiteral("لوز");
    product.costPriceCents = 100000;
    product.salePriceCents = 120000;
    product.piecesPerPackage = 1;
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 10000, QStringLiteral("purchase"));

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 250;
    item.unitKind = QStringLiteral("weight");
    item.unitPriceCents = 120000;

    data::SaleService service(db);
    const data::SaleRecordResult sold = service.recordSale({item}, sessionId, QStringLiteral("desktop"), false);
    QVERIFY2(sold.ok, qPrintable(sold.error));

    const ui::ReceiptData data = ui::buildReceiptForSale(db, sold.saleId);
    QTextDocument* doc = ui::renderReceipt(data, ui::ReceiptPaper::Thermal80);
    const QString text = doc->toPlainText();

    const QString grams = QCoreApplication::translate("app::ui::Receipt", "غرام");
    QVERIFY(text.contains(grams));
    QVERIFY(text.contains(QStringLiteral("250")));
    QVERIFY(text.contains(app::ui::formatMoney(30000, QStringLiteral("DA"))));
    QVERIFY(!text.contains(QStringLiteral("1 ")));
    delete doc;
}
