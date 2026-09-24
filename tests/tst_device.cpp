#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QSqlQuery>

#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/device_ledger_service.h"
#include "data/product_repository.h"
#include "data/payment_repository.h"
#include "data/sale_repository.h"
#include "data/stock_movement_repository.h"
#include "data/sync_outbox_repository.h"
#include "data/sale_item_repository.h"

using namespace app;

class DeviceLedgerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void recordSaleIsAtomicInOutbox();
    void failedSaleRollsBackEverythingIncludingSequence();
    void durableOpIdSurvivesReopen();
    void debtQueuesWithoutCashMovement();
    void paymentRequiresOpenSession();
    void cleanup();

private:
    void seedDatabase();

    QTemporaryDir m_dir;
    QString m_dbPath;
    QString m_deviceId;
    int m_productId = 0;
    int m_customerId = 0;
    int m_openSessionId = 0;
};

void DeviceLedgerTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_dbPath = m_dir.filePath(QStringLiteral("device.sqlite"));
    m_deviceId = QStringLiteral("phone-1");
    seedDatabase();
}

void DeviceLedgerTest::seedDatabase()
{
    QFile::remove(m_dbPath);

    data::Database db(m_dbPath);
    data::CashSessionRepository sessions(db);
    m_openSessionId = sessions.open(5000);
    QVERIFY(m_openSessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000003");
    product.name = QStringLiteral("حليب");
    product.costPriceCents = 9500;
    product.salePriceCents = 12000;
    product.unit = QStringLiteral("وحدة");
    product.packageSize = 1;
    m_productId = products.save(product);
    QVERIFY(m_productId > 0);

    core::StockMovement inbound;
    inbound.productId = m_productId;
    inbound.delta = 100;
    inbound.reason = QStringLiteral("initial stock");
    inbound.createdAt = QDateTime::currentDateTimeUtc();
    data::StockMovementRepository movements(db);
    QVERIFY(movements.insert(inbound) > 0);

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("عميل");
    m_customerId = customers.save(customer);
    QVERIFY(m_customerId > 0);
}

void DeviceLedgerTest::cleanup()
{
    seedDatabase();
}

void DeviceLedgerTest::recordSaleIsAtomicInOutbox()
{
    data::Database db(m_dbPath);
    data::DeviceLedgerService service(db, m_deviceId);

    core::SaleItem item;
    item.productId = m_productId;
    item.quantity = 2;
    item.unitPriceCents = 0; // resolved from product.salePriceCents

    const data::DeviceOpResult result =
        service.recordSale({ item }, m_openSessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.opId > 0);
    QCOMPARE(result.totalCents, 24000LL);
    QCOMPARE(result.cogsCents, 19000LL);

    // Ledger rows all land.
    data::SaleRepository sales(db);
    QVERIFY(sales.findById(result.entityId).has_value());
    data::StockMovementRepository movements(db);
    QCOMPARE(movements.sumByProductId(m_productId), 100 - 2);
    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 24000);

    // Exactly one outbox row exists, carrying the same opId for the server.
    data::SyncOutboxRepository outbox(db);
    QCOMPARE(outbox.countPending(), 1);
    const auto entries = outbox.findPending(10);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries[0].op.opId, result.opId);
    QCOMPARE(entries[0].op.type, core::SyncOpType::Sale);
    QCOMPARE(entries[0].op.deviceId, m_deviceId);
    QCOMPARE(entries[0].op.amountCents, 24000LL);
    QCOMPARE(entries[0].op.items.size(), 1);
    QCOMPARE(entries[0].op.items[0].productId, m_productId);
    QCOMPARE(entries[0].op.items[0].unitPriceCents, 12000LL);
}

void DeviceLedgerTest::failedSaleRollsBackEverythingIncludingSequence()
{
    data::Database db(m_dbPath);
    data::DeviceLedgerService service(db, m_deviceId);

    // Oversold: 500 units but only 100 in stock. The device rejects it exactly
    // like the Windows server would, so no divergence can ever form.
    core::SaleItem item;
    item.productId = m_productId;
    item.quantity = 500;
    item.unitPriceCents = 12000;

    const data::DeviceOpResult failed = service.recordSale({ item }, m_openSessionId);
    QVERIFY(!failed.ok);

    // No sale row, no stock movement, no cash, no outbox row.
    data::SaleRepository sales(db);
    QCOMPARE(sales.findAll().size(), 0);
    data::StockMovementRepository movements(db);
    QCOMPARE(movements.sumByProductId(m_productId), 100);
    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 0);
    data::SyncOutboxRepository outbox(db);
    QCOMPARE(outbox.countPending(), 0);

    // The sync_sequence UPDATE lived in the rolled-back transaction, so the
    // next minted opId is still 1: nothing was consumed silently.
    core::SaleItem okItem = item;
    okItem.quantity = 1;
    const data::DeviceOpResult ok = service.recordSale({ okItem }, m_openSessionId);
    QVERIFY2(ok.ok, qPrintable(ok.error));
    QCOMPARE(ok.opId, 1);
}

void DeviceLedgerTest::durableOpIdSurvivesReopen()
{
    core::SaleItem item;
    item.productId = m_productId;
    item.quantity = 1;
    item.unitPriceCents = 0;

    data::DeviceOpResult first;
    {
        data::Database db(m_dbPath);
        data::DeviceLedgerService service(db, m_deviceId);
        first = service.recordSale({ item }, m_openSessionId);
    }
    QVERIFY2(first.ok, qPrintable(first.error));
    QCOMPARE(first.opId, 1);

    // Simulate a power cycle: close the database and reopen the same file.
    data::DeviceOpResult second;
    {
        data::Database db(m_dbPath);
        data::DeviceLedgerService service(db, m_deviceId);
        second = service.recordSale({ item }, m_openSessionId);
    }
    QVERIFY2(second.ok, qPrintable(second.error));
    QCOMPARE(second.opId, 2);

    // Both queued ops share one sequence, so the server sees distinct opIds.
    data::Database db(m_dbPath);
    data::SyncOutboxRepository outbox(db);
    const auto entries = outbox.findPending(10);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].op.opId, 1);
    QCOMPARE(entries[1].op.opId, 2);
}

void DeviceLedgerTest::debtQueuesWithoutCashMovement()
{
    data::Database db(m_dbPath);
    data::DeviceLedgerService service(db, m_deviceId);

    core::SaleItem item;
    item.productId = m_productId;
    item.quantity = 1;
    item.unitPriceCents = 0;

    const data::DeviceOpResult result = service.recordCustomerDebt(m_customerId, { item });
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.opId > 0);

    // Debts move stock but never cash.
    data::StockMovementRepository movements(db);
    QCOMPARE(movements.sumByProductId(m_productId), 99);
    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 0);

    // The queued op targets the customer id, as the server expects.
    data::SyncOutboxRepository outbox(db);
    const auto entries = outbox.findPending(10);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries[0].op.type, core::SyncOpType::CustomerDebt);
    QCOMPARE(entries[0].op.entityId, m_customerId);
}

void DeviceLedgerTest::paymentRequiresOpenSession()
{
    data::Database db(m_dbPath);
    data::DeviceLedgerService service(db, m_deviceId);

    // No open session -> rejected, and nothing queued.
    data::CashSessionRepository sessions(db);
    QVERIFY(sessions.close(m_openSessionId, 5000, 5000, 0));

    const data::DeviceOpResult result = service.recordCustomerPayment(m_customerId, 10000, m_openSessionId,
                                                                      QStringLiteral("دفعة"));
    QVERIFY(!result.ok);
    data::SyncOutboxRepository outbox(db);
    QCOMPARE(outbox.countPending(), 0);
    data::PaymentRepository payments(db);
    QCOMPARE(payments.findByCustomerId(m_customerId).size(), 0);
}

QTEST_GUILESS_MAIN(DeviceLedgerTest)
#include "tst_device.moc"