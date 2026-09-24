#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/product_repository.h"
#include "data/sale_repository.h"
#include "data/stock_movement_repository.h"
#include "network/sync_processor.h"
#include "network/sync_protocol.h"
#include "network/sync_server.h"

using namespace app;

class SyncServerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void hmacValidProcessesBatch();
    void hmacInvalidRejected();
    void emptyBatchRejected();
    void oversizedBatchRejected();
    void saleAppliesSingleTransaction();
    void debtAppliesWithoutCashSession();
    void paymentRequiresOpenSession();
    void httpPostAppliesBatch();
    void cleanup();

private:
    void seedDatabase();
    QJsonObject saleOp(int opId, const QByteArray& deviceId, int productId,
                       long long quantity, long long unitPriceCents);
    QJsonObject debtOp(int opId, int customerId, const QByteArray& deviceId, int productId,
                       long long quantity, long long unitPriceCents);
    QJsonObject paymentOp(int opId, int customerId, long long amountCents, const QByteArray& note,
                          const QByteArray& deviceId);

    QTemporaryDir m_dir;
    QString m_dbPath;
    QByteArray m_key;
    QByteArray m_deviceId;
    int m_productId = 0;
    int m_customerId = 0;
    int m_openSessionId = 0;
};

void SyncServerTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_dbPath = m_dir.filePath(QStringLiteral("sync.sqlite"));
    m_key = QByteArrayLiteral("test-shared-key");
    m_deviceId = QByteArrayLiteral("device-01");
    seedDatabase();
}

void SyncServerTest::seedDatabase()
{
    QFile::remove(m_dbPath);

    data::Database db(m_dbPath);
    data::CashSessionRepository sessions(db);
    m_openSessionId = sessions.open(5000);
    QVERIFY(m_openSessionId > 0);

    data::ProductRepository products(db);
    app::core::Product product;
    product.barcode = QStringLiteral("6130000000001");
    product.name = QStringLiteral("سلعة");
    product.costPriceCents = 9500;
    product.salePriceCents = 12000;
    product.unit = QStringLiteral("وحدة");
    product.packageSize = 1;
    m_productId = products.save(product);
    QVERIFY(m_productId > 0);

    app::core::StockMovement inbound;
    inbound.productId = m_productId;
    inbound.delta = 100;
    inbound.reason = QStringLiteral("initial stock");
    inbound.createdAt = QDateTime::currentDateTimeUtc();
    data::StockMovementRepository movements(db);
    QVERIFY(movements.insert(inbound) > 0);

    data::CustomerRepository customers(db);
    app::core::Customer customer;
    customer.name = QStringLiteral("عميل");
    m_customerId = customers.save(customer);
    QVERIFY(m_customerId > 0);
}

void SyncServerTest::cleanup()
{
    seedDatabase();
}

QJsonObject SyncServerTest::saleOp(int opId, const QByteArray& deviceId, int productId,
                                   long long quantity, long long unitPriceCents)
{
    QJsonObject item;
    item.insert(QStringLiteral("productId"), productId);
    item.insert(QStringLiteral("quantity"), QJsonValue(static_cast<double>(quantity)));
    item.insert(QStringLiteral("unitPriceCents"), QJsonValue(static_cast<double>(unitPriceCents)));

    QJsonObject op;
    op.insert(QStringLiteral("opId"), opId);
    op.insert(QStringLiteral("type"), static_cast<int>(core::SyncOpType::Sale));
    op.insert(QStringLiteral("entityId"), 0);
    op.insert(QStringLiteral("amountCents"), QJsonValue(static_cast<double>(quantity * unitPriceCents)));
    op.insert(QStringLiteral("occurredAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    op.insert(QStringLiteral("note"), QStringLiteral("sale"));
    op.insert(QStringLiteral("deviceId"), QString::fromUtf8(deviceId));
    op.insert(QStringLiteral("items"), QJsonArray{ item });
    return op;
}

QJsonObject SyncServerTest::debtOp(int opId, int customerId, const QByteArray& deviceId, int productId,
                                   long long quantity, long long unitPriceCents)
{
    QJsonObject item;
    item.insert(QStringLiteral("productId"), productId);
    item.insert(QStringLiteral("quantity"), QJsonValue(static_cast<double>(quantity)));
    item.insert(QStringLiteral("unitPriceCents"), QJsonValue(static_cast<double>(unitPriceCents)));

    QJsonObject op;
    op.insert(QStringLiteral("opId"), opId);
    op.insert(QStringLiteral("type"), static_cast<int>(core::SyncOpType::CustomerDebt));
    op.insert(QStringLiteral("entityId"), customerId);
    op.insert(QStringLiteral("amountCents"), QJsonValue(static_cast<double>(quantity * unitPriceCents)));
    op.insert(QStringLiteral("occurredAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    op.insert(QStringLiteral("note"), QStringLiteral("debt"));
    op.insert(QStringLiteral("deviceId"), QString::fromUtf8(deviceId));
    op.insert(QStringLiteral("items"), QJsonArray{ item });
    return op;
}

QJsonObject SyncServerTest::paymentOp(int opId, int customerId, long long amountCents,
                                      const QByteArray& note, const QByteArray& deviceId)
{
    QJsonObject op;
    op.insert(QStringLiteral("opId"), opId);
    op.insert(QStringLiteral("type"), static_cast<int>(core::SyncOpType::CustomerPayment));
    op.insert(QStringLiteral("entityId"), customerId);
    op.insert(QStringLiteral("amountCents"), QJsonValue(static_cast<double>(amountCents)));
    op.insert(QStringLiteral("occurredAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    op.insert(QStringLiteral("note"), QString::fromUtf8(note));
    op.insert(QStringLiteral("items"), QJsonArray{});
    Q_UNUSED(deviceId)
    return op;
}

void SyncServerTest::hmacValidProcessesBatch()
{
    data::Database db(m_dbPath);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(saleOp(1, m_deviceId, m_productId, 1, 12000));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);

    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY(result.hmacValid);
    QVERIFY(result.batchValid);
    QCOMPARE(result.applied.size(), 1);
    QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.applied[0].type, QStringLiteral("sale"));
    QCOMPARE(result.applied[0].totalCents, 12000);
    QCOMPARE(result.applied[0].cogsCents, 9500);
}

void SyncServerTest::hmacInvalidRejected()
{
    data::Database db(m_dbPath);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(saleOp(1, m_deviceId, m_productId, 1, 12000));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, QByteArrayLiteral("wrong-key"));

    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY(!result.hmacValid);
    QVERIFY(result.applied.isEmpty());
}

void SyncServerTest::emptyBatchRejected()
{
    data::Database db(m_dbPath);
    network::SyncProcessor processor(db);

    const QByteArray body = QByteArrayLiteral("[]");
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);
    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY(result.hmacValid);
    QVERIFY(!result.batchValid);
    QVERIFY(!result.error.isEmpty());
}

void SyncServerTest::oversizedBatchRejected()
{
    data::Database db(m_dbPath);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    for (int i = 0; i < 51; ++i) {
        ops.append(saleOp(i, m_deviceId, m_productId, 1, 12000));
    }

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);
    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY(result.hmacValid);
    QVERIFY(!result.batchValid);
}

void SyncServerTest::saleAppliesSingleTransaction()
{
    data::Database db(m_dbPath);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(saleOp(1, m_deviceId, m_productId, 2, 12000));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);

    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY(result.hmacValid);
    QVERIFY(result.batchValid);
    QCOMPARE(result.applied.size(), 1);

    // Sale row exists.
    data::SaleRepository sales(db);
    QVERIFY(sales.findById(result.applied[0].entityId).has_value());

    // Stock went down by 2 from the seeded 100.
    data::StockMovementRepository movements(db);
    QCOMPARE(movements.sumByProductId(m_productId), 100 - 2);

    // Cash session got +24000 in sale cash.
    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 24000);
}

void SyncServerTest::debtAppliesWithoutCashSession()
{
    data::Database db(m_dbPath);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(debtOp(1, m_customerId, m_deviceId, m_productId, 1, 12000));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);

    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY(result.hmacValid);
    QVERIFY(result.batchValid);
    QCOMPARE(result.applied.size(), 1);

    // No cash movement for a debt-only op.
    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 0);
}

void SyncServerTest::paymentRequiresOpenSession()
{
    data::Database db(m_dbPath);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(paymentOp(1, m_customerId, 3000, "دفعة", m_deviceId));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);

    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY(result.hmacValid);
    QVERIFY(result.batchValid);
    QCOMPARE(result.applied.size(), 1);
    QCOMPARE(result.applied[0].type, QStringLiteral("customer_payment"));
    QCOMPARE(result.applied[0].totalCents, 3000);

    // Payment added +3000 to the open session cash.
    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 3000);
}

void SyncServerTest::httpPostAppliesBatch()
{
    data::Database db(m_dbPath);
    network::SyncServer server(db, m_key);
    const quint16 port = server.server().listen(QHostAddress::LocalHost, 0);
    QVERIFY(port != 0);

    QJsonArray ops;
    ops.append(saleOp(7, m_deviceId, m_productId, 1, 12000));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);

    QNetworkAccessManager manager;
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(port);
    url.setPath(QStringLiteral("/api/sync"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("X-Mahali-Signature", signature.toHex());

    QNetworkReply* reply = manager.post(request, body);
    QSignalSpy finished(reply, &QNetworkReply::finished);
    QVERIFY(finished.wait(5000));
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QCOMPARE(status, 200);

    const QJsonDocument replyDoc = QJsonDocument::fromJson(reply->readAll());
    QVERIFY(replyDoc.isObject());
    QVERIFY(replyDoc.object().value(QStringLiteral("ok")).toBool());
    QCOMPARE(replyDoc.object().value(QStringLiteral("applied")).toArray().size(), 1);

    reply->deleteLater();
}

QTEST_GUILESS_MAIN(SyncServerTest)
#include "tst_sync_server.moc"