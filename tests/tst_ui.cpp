#include <QtTest/QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QUrl>

#include "core/sync_operation.h"
#include "data/applied_op_repository.h"
#include "data/audit_log_repository.h"
#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/customer_transaction_repository.h"
#include "data/payment_repository.h"
#include "data/product_repository.h"
#include "data/sale_item_repository.h"
#include "data/sale_repository.h"
#include "data/sale_service.h"
#include "data/stock_movement_repository.h"
#include "data/supplier_repository.h"
#include "data/supplier_transaction_repository.h"
#include "network/sync_client.h"
#include "ui/cash_session_page.h"
#include "ui/customers_page.h"
#include "ui/format_utils.h"
#include "ui/pos_page.h"
#include "ui/products_page.h"
#include "ui/sales_page.h"
#include "ui/server_controller.h"
#include "ui/suppliers_page.h"

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
    void posSaleRequiresOpenSession();
    void cashSessionLifecycle();
    void salesPageShowsToday();

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

    core::SupplierTransaction invoice;
    invoice.supplierId = supplierId;
    invoice.amountCents = 250000;
    invoice.note = QStringLiteral("فاتورة");
    invoice.createdAt = QDateTime::currentDateTime();
    QVERIFY(data::SupplierTransactionRepository(db).insert(invoice) > 0);
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
    suppliers.suppliersTable()->setCurrentCell(0, 0);
    QCOMPARE(suppliers.transactionCount(), 1);
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

    // Cashier edits quantity and overrides the unit price in the grid.
    page.table()->item(0, 1)->setText(QStringLiteral("2"));
    page.table()->item(0, 2)->setText(QStringLiteral("7.50"));
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

QTEST_MAIN(UiTest)
#include "tst_ui.moc"