#include <QtTest/QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <functional>

#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/device_identity.h"
#include "data/device_ledger_service.h"
#include "data/product_repository.h"
#include "data/sale_repository.h"
#include "data/stock_movement_repository.h"
#include "data/sync_outbox_repository.h"
#include "network/sync_scheduler.h"
#include "network/sync_server.h"

using namespace app;

// Phase 9: the device's own sync lifecycle. Once DeviceLedgerService has written
// an op and queued its outbox row, SyncScheduler keeps pushing it in the
// background — retrying with an exponential backoff while the LAN is dead, then
// draining exactly once when the server returns. The device identity (device_id)
// is persisted in its settings the first time the device ever opens.
class SyncSchedulerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void identityPersistsAcrossReopens();
    void autoSyncDrainsOutboxAndAppliesServerSide();
    void backoffRetriesWhileOfflineThenRecovers();

private:
    void seed(const QString& path, int* productId, int* sessionId);
    void waitFor(const std::function<bool()>& cond, int timeoutMs);
    static QUrl endpointFor(quint16 port);
    static QUrl deadEndpoint();

    QTemporaryDir m_dir;
    QString m_deviceDbPath;
    QString m_serverDbPath;
    QByteArray m_key;
};

void SyncSchedulerTest::initTestCase()
{
    qRegisterMetaType<network::SyncDrainResult>("app::network::SyncDrainResult");
    QVERIFY(m_dir.isValid());
    m_deviceDbPath = m_dir.filePath(QStringLiteral("device.sqlite"));
    m_serverDbPath = m_dir.filePath(QStringLiteral("server.sqlite"));
    m_key = QByteArrayLiteral("phase9-scheduler-key");
}

void SyncSchedulerTest::seed(const QString& path, int* productId, int* sessionId)
{
    QFile::remove(path);
    data::Database db(path);
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

    core::StockMovement inbound;
    inbound.productId = *productId;
    inbound.delta = 100;
    inbound.reason = QStringLiteral("initial stock");
    inbound.createdAt = QDateTime::currentDateTimeUtc();
    data::StockMovementRepository movements(db);
    QVERIFY(movements.insert(inbound) > 0);
}

void SyncSchedulerTest::waitFor(const std::function<bool()>& cond, int timeoutMs)
{
    const int step = 25;
    int waited = 0;
    while (!cond() && waited < timeoutMs) {
        QTest::qWait(step);
        waited += step;
    }
    QVERIFY2(cond(), "condition not satisfied within timeout");
}

QUrl SyncSchedulerTest::endpointFor(quint16 port)
{
    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(QStringLiteral("127.0.0.1"));
    endpoint.setPort(port);
    endpoint.setPath(QStringLiteral("/api/sync"));
    return endpoint;
}

QUrl SyncSchedulerTest::deadEndpoint()
{
    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(QStringLiteral("127.0.0.1"));
    endpoint.setPort(1); // nothing listens here
    endpoint.setPath(QStringLiteral("/api/sync"));
    return endpoint;
}

void SyncSchedulerTest::identityPersistsAcrossReopens()
{
    // The device mints its id once and never changes it, even across restarts.
    data::Database first(m_deviceDbPath);
    const QString id1 = data::DeviceIdentity::ensure(first);
    QVERIFY(!id1.isEmpty());

    data::Database reopened(m_deviceDbPath);
    QCOMPARE(data::DeviceIdentity::ensure(reopened), id1);

    // A different store is a different device.
    const QString other = m_dir.filePath(QStringLiteral("other-device.sqlite"));
    QFile::remove(other);
    data::Database otherDb(other);
    QVERIFY(data::DeviceIdentity::ensure(otherDb) != id1);

    // The default DeviceLedgerService ctor speaks under the same identity.
    QCOMPARE(data::DeviceIdentity::ensure(first), id1);
}

void SyncSchedulerTest::autoSyncDrainsOutboxAndAppliesServerSide()
{
    int productId = 0;
    int sessionId = 0;
    seed(m_deviceDbPath, &productId, &sessionId);
    seed(m_serverDbPath, &productId, &sessionId);

    // Record the sale through the identity-aware ctor (no explicit deviceId).
    data::Database device(m_deviceDbPath);
    const QString deviceId = data::DeviceIdentity::ensure(device);
    data::DeviceLedgerService ledger(device);
    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 0; // resolved from product.salePriceCents
    const data::DeviceOpResult recorded = ledger.recordSale({ item }, sessionId);
    QVERIFY2(recorded.ok, qPrintable(recorded.error));
    QCOMPARE(recorded.opId, 1);
    data::SyncOutboxRepository outbox(device);
    QCOMPARE(outbox.countPending(), 1);

    data::Database server(m_serverDbPath, data::DatabaseMode::Server);
    network::SyncServer syncServer(server, m_key);
    const quint16 port = syncServer.server().listen(QHostAddress::LocalHost, 0);
    QVERIFY(port != 0);

    network::SyncScheduler scheduler(device, endpointFor(port), m_key);
    scheduler.setBaseInterval(50);
    scheduler.setMaxInterval(200);
    QSignalSpy spy(&scheduler, &network::SyncScheduler::syncCompleted);
    scheduler.start();

    waitFor([&] { return outbox.countPending() == 0; }, 4000);
    scheduler.stop();

    QVERIFY(spy.count() >= 1);
    const auto result = spy.first().at(0).value<network::SyncDrainResult>();
    QVERIFY(result.delivered);
    QCOMPARE(result.errorClass, network::SyncErrorClass::None);
    QCOMPARE(result.applied, 1);
    QCOMPARE(outbox.countPending(), 0);

    // The identity reached the server and the ledger effects are applied once.
    data::SaleRepository sales(server);
    bool foundSale = false;
    for (const core::Sale& sale : sales.findAll()) {
        if (sale.deviceId == deviceId && sale.totalCents == 20000) {
            foundSale = true;
        }
    }
    QVERIFY(foundSale);
    data::StockMovementRepository movements(server);
    QCOMPARE(movements.sumByProductId(productId), 100 - 2);
    data::CashMovementRepository cash(server);
    QCOMPARE(cash.sumBySessionId(sessionId), 20000);
}

void SyncSchedulerTest::backoffRetriesWhileOfflineThenRecovers()
{
    int productId = 0;
    int sessionId = 0;
    seed(m_deviceDbPath, &productId, &sessionId);
    data::Database device(m_deviceDbPath);
    data::DeviceLedgerService ledger(device, QStringLiteral("pos-device-B"));
    core::SaleItem item;
    item.productId = productId;
    item.quantity = 1;
    item.unitPriceCents = 0;
    const data::DeviceOpResult recorded = ledger.recordSale({ item }, sessionId);
    QVERIFY2(recorded.ok, qPrintable(recorded.error));

    // LAN down: the scheduler keeps retrying per the backoff, never losing the op.
    network::SyncScheduler scheduler(device, deadEndpoint(), m_key);
    scheduler.setBaseInterval(50);
    scheduler.setMaxInterval(200);
    scheduler.start();

    data::SyncOutboxRepository outbox(device);
    QTest::qWait(500);
    scheduler.stop();
    QCOMPARE(outbox.countPending(), 1); // nothing lost
    const auto retried = outbox.findPending(1);
    QVERIFY(retried.size() == 1);
    QVERIFY2(retried[0].attempts >= 2, "scheduler should have retried at least twice while offline");
    QVERIFY2(scheduler.currentInterval() > scheduler.baseIntervalForTest(),
             "backoff should have grown while the server was unreachable");

    // The server comes back: the loop catches up by itself.
    seed(m_serverDbPath, &productId, &sessionId);
    data::Database server(m_serverDbPath, data::DatabaseMode::Server);
    network::SyncServer syncServer(server, m_key);
    const quint16 port = syncServer.server().listen(QHostAddress::LocalHost, 0);
    QVERIFY(port != 0);

    network::SyncScheduler resumed(device, endpointFor(port), m_key);
    resumed.setBaseInterval(50);
    resumed.setMaxInterval(200);
    resumed.start();
    waitFor([&] { return outbox.countPending() == 0; }, 4000);
    resumed.stop();

    data::SaleRepository sales(server);
    QCOMPARE(sales.countByDeviceId(QString::fromUtf8("pos-device-B")), 1);
}

QTEST_GUILESS_MAIN(SyncSchedulerTest)
#include "tst_scheduler.moc"