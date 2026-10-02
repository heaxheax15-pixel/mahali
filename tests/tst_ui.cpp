 #include <QtTest/QtTest>

#include <QDate>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTime>
#include <QComboBox>
#include <QTableView>
#include <QTableWidget>
#include <QUrl>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QMouseEvent>
#include <QTimer>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include "ui/login_dialog.h"
#include "ui/users_page.h"

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
#include "data/zakat_setting_repository.h"
#include "data/user_repository.h"
#include "network/sync_client.h"
#include "ui/audit_log_page.h"
#include "ui/cash_session_page.h"
#include "ui/customers_page.h"
#include "ui/expenses_page.h"
#include "core/format_utils.h"
#include "ui/pos_page.h"
#include "ui/products_page.h"
#include "ui/quick_items_bar.h"
#include "ui/refunds_page.h"
#include "ui/reports_page.h"
#include "ui/sales_page.h"
#include "ui/server_controller.h"
#include "ui/settings_page.h"
#include "ui/suppliers_page.h"
#include "ui/main_window.h"

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
    void serverStartStop();
    void serverAppliesOverLoopback();
    void serverEmitsStatsOnTimer();
    void serverRetentionPrunesOldOps();
    void pagesReflectSeededData();
    void flexibleAmountParsing();
    void posSaleWithPriceOverride();
    void pos_cart_grid_is_the_wide_table_and_the_narrow_one_is_empty();
    void posSaleRequiresOpenSession();
    void cashSessionLifecycle();
    void cash_session_rejects_invalid_amount();
    void salesPageShowsToday();
    void profit_cents_survives_translation();
    void barcode_dialog_does_not_close();
    void quick_items_bar_lists_products();
    void customerCreditAndPayment();
    void expensesAndDrawings();
    void reportBuilds();
    void settingsCurrencyAndZakat();
    void update_bar_hidden_by_default();
    void update_bar_appears_on_signal();
    void refundsRestoreMoneyAndStock();
    void paymentRefundRaisesBalance();
    void entryReversal();
    void login_basic();
    // void statusBarSwitchUserButton();
    void users_page_lists_users();
    void users_page_shows_all_pins();
    void user_edit_pin_persists();
    void master_recovery_grants_access();
    void status_bar_shows_occasion_when_active();

private:
    void seedSyncDatabase(const QString& path, int* productId, int* sessionId);
    void seedMasterData(const QString& path);
    int seedProduct(const QString& path, int* sessionId, long long salePriceCents);

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
    QCOMPARE(grid->horizontalHeaderItem(2)->text(), QStringLiteral("Unité"));
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
    QCOMPARE(grid->item(0, 2)->text(), QStringLiteral("علبة"));
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

// A barcode scanner terminates every scan with Enter. In a QDialog that Enter used
// to hit the default (OK) button, saving and closing the form mid-entry.
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

    QCOMPARE(report.revenueCents, 10000LL);
    QCOMPARE(report.salesCount, 1LL);
    QCOMPARE(report.cogsCents, 6000LL);
    QCOMPARE(report.grossProfitCents, 4000LL);
    QCOMPARE(report.expensesCents, 1500LL);
    QCOMPARE(report.drawingsCents, 2000LL);
    QCOMPARE(report.netProfitCents, 2500LL);
    QCOMPARE(report.outstandingDebtCents, 3000LL);
    QCOMPARE(report.zakatBaseCents, 13000LL);
    QCOMPARE(report.zakatCents, 325LL);
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
    QCOMPARE(page.report().revenueCents, 10000LL);
    QCOMPARE(page.report().zakatCents, 325LL);
    QCOMPARE(page.report().netProfitCents, 2500LL);
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
    data::ZakatSettingRepository zakat(db);
    const auto enabledRow = zakat.findByKey(QStringLiteral("enabled"));
    QVERIFY(enabledRow.has_value());
    QCOMPARE(enabledRow->value, QStringLiteral("1"));

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
    QCOMPARE(report.zakatBaseCents, 10000LL);
    QCOMPARE(report.zakatCents, 0LL);

    app::ui::setCurrencySymbol(QString());
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

void UiTest::users_page_shows_all_pins()
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

    // Every row must show its own PIN, whatever the role.
    QTableWidget* table = page.table();
    QVERIFY(table);

    QHash<QString, QString> pinByName;
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* nameItem = table->item(row, 0);
        QTableWidgetItem* pinItem = table->item(row, 2);
        QVERIFY2(nameItem, qPrintable(QStringLiteral("missing name cell on row %1").arg(row)));
        QVERIFY2(pinItem, qPrintable(QStringLiteral("missing PIN cell on row %1").arg(row)));
        pinByName.insert(nameItem->text(), pinItem->text());
    }

    QCOMPARE(pinByName.size(), seeds.size());
    for (const Seed& seed : seeds) {
        QVERIFY2(pinByName.contains(seed.name),
                 qPrintable(QStringLiteral("no row for user %1").arg(seed.name)));
        QCOMPARE(pinByName.value(seed.name), seed.pin);
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

    QWidget* actions = table->cellWidget(0, 4);
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

    // And the users table must now render the new PIN.
    page.refresh();
    QTableWidgetItem* nameItem = table->item(0, 0);
    QTableWidgetItem* pinItem = table->item(0, 2);
    QVERIFY(nameItem);
    QVERIFY(pinItem);
    QCOMPARE(nameItem->text(), QStringLiteral("المدير"));
    QCOMPARE(pinItem->text(), QStringLiteral("34"));
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

QTEST_MAIN(UiTest)
#include "tst_ui.moc"