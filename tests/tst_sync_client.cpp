#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QUrl>

#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/product_repository.h"
#include "data/sale_repository.h"
#include "data/stock_movement_repository.h"
#include "data/sync_outbox_repository.h"
#include "network/sync_client.h"
#include "network/sync_server.h"

using namespace app;

class SyncClientTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void outboxEnqueueAndFind();
    void outboxMarkStates();
    void sendBatchAppliesToServer();
    void sendBatchWrongKeyPermanent();
    void sendBatchEmptyIsPermanent();
    void sendBatchOversizedIsPermanent();
    void sendToDeadServerIsNetwork();
    void cleanup();

private:
    void seedDatabase();

    QTemporaryDir m_dir;
    QString m_dbPath;
    QByteArray m_key;
    QByteArray m_deviceId;
    int m_productId = 0;
    int m_customerId = 0;
    int m_openSessionId = 0;
};

void SyncClientTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_dbPath = m_dir.filePath(QStringLiteral("client.sqlite"));
    m_key = QByteArrayLiteral("phase6-shared-key");
    m_deviceId = QByteArrayLiteral("device-phone-1");
    seedDatabase();
}

void SyncClientTest::seedDatabase()
{
    QFile::remove(m_dbPath);

    data::Database db(m_dbPath);
    data::CashSessionRepository sessions(db);
    m_openSessionId = sessions.open(5000);
    QVERIFY(m_openSessionId > 0);

    data::ProductRepository products(db);
    app::core::Product product;
    product.barcode = QStringLiteral("6130000000002");
    product.name = QStringLiteral("زبادي");
    product.costPriceCents = 3000;
    product.salePriceCents = 4500;
    product.unit = QStringLiteral("وعاء");
    product.packageSize = 1;
    m_productId = products.save(product);
    QVERIFY(m_productId > 0);

    app::core::StockMovement inbound;
    inbound.productId = m_productId;
    inbound.delta = 50;
    inbound.reason = QStringLiteral("initial stock");
    inbound.createdAt = QDateTime::currentDateTimeUtc();
    data::StockMovementRepository movements(db);
    QVERIFY(movements.insert(inbound) > 0);

    data::CustomerRepository customers(db);
    app::core::Customer customer;
    customer.name = QStringLiteral("زبون");
    m_customerId = customers.save(customer);
    QVERIFY(m_customerId > 0);
}

void SyncClientTest::cleanup()
{
    seedDatabase();
}

app::core::SyncOperation makeOp(int opId, app::core::SyncOpType type, bool withItems = false)
{
    app::core::SyncOperation op;
    op.opId = opId;
    op.type = type;
    op.entityId = 0;
    op.amountCents = 0;
    op.occurredAt = QDateTime::currentDateTimeUtc();
    op.note = QStringLiteral("test");
    op.deviceId = QStringLiteral("device-phone-1");
    if (withItems) {
        app::core::SyncItem item;
        item.productId = 0;
        item.quantity = 1;
        item.unitPriceCents = 0;
        op.items.append(item);
    }
    return op;
}

void SyncClientTest::outboxEnqueueAndFind()
{
    data::Database db(m_dbPath);
    data::SyncOutboxRepository outbox(db);

    QCOMPARE(outbox.countPending(), 0);

    app::core::SyncOperation opA = makeOp(1, app::core::SyncOpType::Sale, true);
    app::core::SyncOperation opB = makeOp(2, app::core::SyncOpType::CustomerPayment);
    const int idA = outbox.enqueue(opA);
    const int idB = outbox.enqueue(opB);
    QVERIFY(idA > 0);
    QVERIFY(idB > 0);
    QCOMPARE(outbox.countPending(), 2);

    const auto entries = outbox.findPending(10);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].id, idA);
    QCOMPARE(entries[0].op.opId, 1);
    QCOMPARE(entries[0].op.type, app::core::SyncOpType::Sale);
    QCOMPARE(entries[0].op.items.size(), 1);
    QCOMPARE(entries[1].op.opId, 2);
    QCOMPARE(entries[0].status, app::core::SyncOutboxStatus::Pending);
    QCOMPARE(entries[1].status, app::core::SyncOutboxStatus::Pending);

    const auto found = outbox.findById(idB);
    QVERIFY(found.has_value());
    QCOMPARE(found->op.type, app::core::SyncOpType::CustomerPayment);
}

void SyncClientTest::outboxMarkStates()
{
    data::Database db(m_dbPath);
    data::SyncOutboxRepository outbox(db);

    app::core::SyncOperation opOk = makeOp(10, app::core::SyncOpType::Sale, true);
    app::core::SyncOperation opBad = makeOp(11, app::core::SyncOpType::CustomerPayment);
    const int idOk = outbox.enqueue(opOk);
    const int idBad = outbox.enqueue(opBad);
    QCOMPARE(outbox.countPending(), 2);

    QVERIFY(outbox.markApplied(idOk));
    QCOMPARE(outbox.countPending(), 1);

    QVERIFY(outbox.markPermanentFailed(idBad, QStringLiteral("rejected: no cash session")));
    QCOMPARE(outbox.countPending(), 0);

    const auto failed = outbox.findById(idBad);
    QVERIFY(failed.has_value());
    QCOMPARE(failed->status, app::core::SyncOutboxStatus::PermanentFailed);
    QCOMPARE(failed->lastError, QStringLiteral("rejected: no cash session"));
}

void SyncClientTest::sendBatchAppliesToServer()
{
    data::Database db(m_dbPath);
    network::SyncServer server(db, m_key);
    const quint16 port = server.server().listen(QHostAddress::LocalHost, 0);
    QVERIFY(port != 0);

    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(QStringLiteral("127.0.0.1"));
    endpoint.setPort(port);
    endpoint.setPath(QStringLiteral("/api/sync"));

    app::core::SyncOperation sale = makeOp(100, app::core::SyncOpType::Sale);
    sale.amountCents = 9000;
    app::core::SyncItem item;
    item.productId = m_productId;
    item.quantity = 2;
    item.unitPriceCents = 4500;
    sale.items.clear();
    sale.items.append(item);

    const network::SyncSendResult result_ =
        network::SyncClient::sendBatch(endpoint, m_key, QVector<app::core::SyncOperation>{ sale });
    QVERIFY(result_.delivered);
    QCOMPARE(result_.errorClass, network::SyncErrorClass::None);
    QCOMPARE(result_.acks.size(), 1);
    QVERIFY(result_.acks[0].ok);
    QCOMPARE(result_.acks[0].appliedOpId, 100);

    data::SaleRepository sales(db);
    const auto all = sales.findAll();
    bool foundSale = false;
    for (const app::core::Sale& s : all) {
        if (s.deviceId == QStringLiteral("device-phone-1") && s.totalCents == 9000) {
            foundSale = true;
        }
    }
    QVERIFY(foundSale);

    data::StockMovementRepository movements(db);
    QCOMPARE(movements.sumByProductId(m_productId), 50 - 2);

    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 9000);
}

void SyncClientTest::sendBatchWrongKeyPermanent()
{
    data::Database db(m_dbPath);
    network::SyncServer server(db, m_key);
    const quint16 port = server.server().listen(QHostAddress::LocalHost, 0);
    QVERIFY(port != 0);

    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(QStringLiteral("127.0.0.1"));
    endpoint.setPort(port);
    endpoint.setPath(QStringLiteral("/api/sync"));

    app::core::SyncOperation sale = makeOp(200, app::core::SyncOpType::Sale, true);
    const network::SyncSendResult result_ =
        network::SyncClient::sendBatch(endpoint, QByteArrayLiteral("wrong-key"),
                                       QVector<app::core::SyncOperation>{ sale });
    QVERIFY(result_.delivered);
    QCOMPARE(result_.errorClass, network::SyncErrorClass::Permanent);
    QVERIFY(result_.acks.isEmpty());
}

void SyncClientTest::sendBatchEmptyIsPermanent()
{
    const network::SyncSendResult result_ =
        network::SyncClient::sendBatch(QUrl(), m_key, QVector<app::core::SyncOperation>{});
    QVERIFY(!result_.delivered);
    QCOMPARE(result_.errorClass, network::SyncErrorClass::Permanent);
}

void SyncClientTest::sendBatchOversizedIsPermanent()
{
    QVector<app::core::SyncOperation> ops;
    for (int i = 0; i < 51; ++i) {
        ops.append(makeOp(i, app::core::SyncOpType::Sale, true));
    }
    const network::SyncSendResult result_ =
        network::SyncClient::sendBatch(QUrl(), m_key, ops);
    QVERIFY(!result_.delivered);
    QCOMPARE(result_.errorClass, network::SyncErrorClass::Permanent);
}

void SyncClientTest::sendToDeadServerIsNetwork()
{
    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(QStringLiteral("127.0.0.1"));
    endpoint.setPort(1);
    endpoint.setPath(QStringLiteral("/api/sync"));

    app::core::SyncOperation sale = makeOp(300, app::core::SyncOpType::Sale, true);
    const network::SyncSendResult result_ =
        network::SyncClient::sendBatch(endpoint, m_key, QVector<app::core::SyncOperation>{ sale });
    QVERIFY(!result_.delivered);
    QCOMPARE(result_.errorClass, network::SyncErrorClass::Network);
}

QTEST_GUILESS_MAIN(SyncClientTest)
#include "tst_sync_client.moc"