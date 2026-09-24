#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QUrl>

#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/device_ledger_service.h"
#include "data/product_repository.h"
#include "data/sale_repository.h"
#include "data/stock_movement_repository.h"
#include "data/sync_outbox_repository.h"
#include "network/sync_coordinator.h"
#include "network/sync_server.h"

using namespace app;

// End-to-end journey test for the device data pipeline:
//   DeviceLedgerService (atomic device write + outbox enqueue)
//     -> SyncCoordinator (drain + ACK reconciliation)
//     -> SyncServer on localhost (exactly-once apply on the Windows DB)
//     -> outbox rows marked applied / canned / retried.
class SyncCoordinatorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sendsAndReconcilesOneSale();
    void networkLossKeepsPendingThenRecovers();
    void permanentRejectCansTheRow();
    void cleanup();

private:
    void seedBothDatabases();

    QTemporaryDir m_dir;
    QString m_deviceDbPath;
    QString m_serverDbPath;
    QByteArray m_key;
    QByteArray m_deviceId;
    int m_productId = 0;
    int m_customerId = 0;
    int m_openSessionId = 0;
};

void SyncCoordinatorTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_deviceDbPath = m_dir.filePath(QStringLiteral("device.sqlite"));
    m_serverDbPath = m_dir.filePath(QStringLiteral("server.sqlite"));
    m_key = QByteArrayLiteral("phase8-coordinator-key");
    m_deviceId = QByteArrayLiteral("pos-device-1");
    seedBothDatabases();
}

void SyncCoordinatorTest::seedBothDatabases()
{
    QFile::remove(m_deviceDbPath);
    QFile::remove(m_serverDbPath);

    const auto seedOne = [&](const QString& path) {
        data::Database db(path);
        data::CashSessionRepository sessions(db);
        m_openSessionId = sessions.open(5000);
        QVERIFY(m_openSessionId > 0);

        data::ProductRepository products(db);
        core::Product product;
        product.barcode = QStringLiteral("6130000000004");
        product.name = QStringLiteral("معجون");
        product.costPriceCents = 7500;
        product.salePriceCents = 10000;
        product.unit = QStringLiteral("أنبوب");
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
        customer.name = QStringLiteral("زبون");
        m_customerId = customers.save(customer);
        QVERIFY(m_customerId > 0);
    };

    seedOne(m_deviceDbPath);
    seedOne(m_serverDbPath);
}

void SyncCoordinatorTest::cleanup()
{
    seedBothDatabases();
}

QUrl endpointFor(quint16 port)
{
    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(QStringLiteral("127.0.0.1"));
    endpoint.setPort(port);
    endpoint.setPath(QStringLiteral("/api/sync"));
    return endpoint;
}

void SyncCoordinatorTest::sendsAndReconcilesOneSale()
{
    // Device records the sale atomically (ledger + outbox in one transaction).
    data::Database device(m_deviceDbPath);
    data::DeviceLedgerService ledger(device, QString::fromUtf8(m_deviceId));
    core::SaleItem item;
    item.productId = m_productId;
    item.quantity = 2;
    item.unitPriceCents = 0; // resolved from product.salePriceCents
    const data::DeviceOpResult recorded = ledger.recordSale({ item }, m_openSessionId);
    QVERIFY2(recorded.ok, qPrintable(recorded.error));
    QCOMPARE(recorded.opId, 1);

    data::SyncOutboxRepository outbox(device);
    QCOMPARE(outbox.countPending(), 1);

    // Bring up the central Windows server on localhost.
    data::Database server(m_serverDbPath);
    network::SyncServer syncServer(server, m_key);
    const quint16 port = syncServer.server().listen(QHostAddress::LocalHost, 0);
    QVERIFY(port != 0);

    // One drain: outbox -> LAN -> server -> ACK -> row marked applied.
    network::SyncCoordinator coordinator(device, endpointFor(port), m_key);
    const network::SyncDrainResult result = coordinator.drainOnce();
    QVERIFY(result.attempted);
    QVERIFY(result.delivered);
    QCOMPARE(result.errorClass, network::SyncErrorClass::None);
    QCOMPARE(result.sent, 1);
    QCOMPARE(result.applied, 1);
    QCOMPARE(result.permanentFailed, 0);
    QCOMPARE(result.keptPending, 0);

    QCOMPARE(outbox.countPending(), 0);
    const auto entries = outbox.findPending(10);
    QCOMPARE(entries.size(), 0);

    // The server side applied exactly one sale.
    data::SaleRepository sales(server);
    const auto all = sales.findAll();
    bool foundSale = false;
    for (const core::Sale& sale : all) {
        if (sale.deviceId == QString::fromUtf8(m_deviceId) && sale.totalCents == 20000) {
            foundSale = true;
        }
    }
    QVERIFY(foundSale);
    data::StockMovementRepository movements(server);
    QCOMPARE(movements.sumByProductId(m_productId), 100 - 2);
    data::CashMovementRepository cash(server);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 20000);
}

void SyncCoordinatorTest::networkLossKeepsPendingThenRecovers()
{
    data::Database device(m_deviceDbPath);
    data::DeviceLedgerService ledger(device, QString::fromUtf8(m_deviceId));
    core::SaleItem item;
    item.productId = m_productId;
    item.quantity = 1;
    item.unitPriceCents = 0;
    const data::DeviceOpResult recorded = ledger.recordSale({ item }, m_openSessionId);
    QVERIFY2(recorded.ok, qPrintable(recorded.error));
    QCOMPARE(recorded.opId, 1);

    // Power/network cut: no server listening on this port. The drain must not
    // lose anything — every op stays pending and the attempt counter grows.
    QUrl deadEndpoint;
    deadEndpoint.setScheme(QStringLiteral("http"));
    deadEndpoint.setHost(QStringLiteral("127.0.0.1"));
    deadEndpoint.setPort(1); // nothing listens here
    deadEndpoint.setPath(QStringLiteral("/api/sync"));

    network::SyncCoordinator coordinator(device, deadEndpoint, m_key);
    const network::SyncDrainResult lost = coordinator.drainOnce();
    QVERIFY(lost.attempted);
    QVERIFY(!lost.delivered);
    QCOMPARE(lost.errorClass, network::SyncErrorClass::Network);
    QCOMPARE(lost.sent, 1);
    QCOMPARE(lost.applied, 0);
    QCOMPARE(lost.keptPending, 1);

    data::SyncOutboxRepository outbox(device);
    QCOMPARE(outbox.countPending(), 1);
    const auto afterLoss = outbox.findPending(10);
    QCOMPARE(afterLoss[0].attempts, 1);

    // LAN comes back: the very same coordinator resumes and reconciles.
    data::Database server(m_serverDbPath);
    network::SyncServer syncServer(server, m_key);
    const quint16 port = syncServer.server().listen(QHostAddress::LocalHost, 0);
    QVERIFY(port != 0);

    network::SyncCoordinator resume(device, endpointFor(port), m_key);
    const network::SyncDrainResult healed = resume.drainOnce();
    QVERIFY(healed.delivered);
    QCOMPARE(healed.applied, 1);
    QCOMPARE(outbox.countPending(), 0);

    data::SaleRepository sales(server);
    QCOMPARE(sales.countByDeviceId(QString::fromUtf8(m_deviceId)), 1);
}

void SyncCoordinatorTest::permanentRejectCansTheRow()
{
    // A malformed op that the server can never apply (unknown operation type)
    // must be canned as permanent_failed, never retried forever.
    data::Database device(m_deviceDbPath);
    data::SyncOutboxRepository outbox(device);

    core::SyncOperation bad;
    bad.opId = 77;
    bad.type = static_cast<core::SyncOpType>(999);
    bad.amountCents = 0;
    bad.occurredAt = QDateTime::currentDateTimeUtc();
    bad.deviceId = QString::fromUtf8(m_deviceId);
    outbox.enqueue(bad);
    QCOMPARE(outbox.countPending(), 1);

    data::Database server(m_serverDbPath);
    network::SyncServer syncServer(server, m_key);
    const quint16 port = syncServer.server().listen(QHostAddress::LocalHost, 0);
    QVERIFY(port != 0);

    network::SyncCoordinator coordinator(device, endpointFor(port), m_key);
    const network::SyncDrainResult result = coordinator.drainOnce();
    QVERIFY(result.delivered);
    QCOMPARE(result.sent, 1);
    QCOMPARE(result.permanentFailed, 1);
    QCOMPARE(result.applied, 0);
    QCOMPARE(result.keptPending, 0);

    QCOMPARE(outbox.countPending(), 0);
    const auto entry = outbox.findById(1);
    QVERIFY(entry.has_value());
    QCOMPARE(entry->status, core::SyncOutboxStatus::PermanentFailed);
    QVERIFY(!entry->lastError.isEmpty());
}

QTEST_GUILESS_MAIN(SyncCoordinatorTest)
#include "tst_coordinator.moc"