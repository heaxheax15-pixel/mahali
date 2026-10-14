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
#include "data/audit_log_repository.h"
#include "data/customer_repository.h"
#include "data/customer_transaction_repository.h"
#include "data/product_repository.h"
#include "data/sale_item_repository.h"
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
    void package_sale_lands_the_pieces_the_central_definition_says();
    void weight_sale_lands_the_grams_the_central_definition_says();
    void debtAppliesWithoutCashSession();
    void paymentRequiresOpenSession();
    void duplicateBatchAppliedOnce();
    void duplicateAcrossBatchAndReplayAppliesOnce();
    void duplicateInsideSingleBatchAppliesOnce();
    void zeroOpIdRejected();
    void emptyDeviceIdRejected();
    void invalidLegacyNumberIsReturnedAsOperationError();
    void sync_audit_actor_uses_device_id();
    void processJsonReplayAlreadyApplied();
    void httpPostAppliesBatch();
    void cleanup();

private:
    void seedDatabase();
    QJsonObject saleOp(int opId, const QByteArray& deviceId, int productId,
                       long long quantity, long long unitPriceCents);
    QJsonObject saleOpWithUnit(int opId, const QByteArray& deviceId, int productId, long long quantity,
                               long long unitPriceCents, const QString& unitKind, long long piecesConsumed,
                               long long adjustmentCents);
    QJsonObject debtOp(int opId, int customerId, const QByteArray& deviceId, int productId,
                       long long quantity, long long unitPriceCents);
    QJsonObject paymentOp(int opId, int customerId, long long amountCents, const QByteArray& note,
                          const QByteArray& deviceId);

    QTemporaryDir m_dir;
    QString m_dbPath;
    QByteArray m_key;
    QByteArray m_deviceId;
    int m_productId = 0;
    // A carton of 24 sold at 1440 the carton, so 2 cartons are 48 pieces off
    // the shelf and 2880 cents in the drawer.
    int m_packageProductId = 0;
    // A weight product priced at 120000 cents (1200.00) a kilogram, with grams
    // on the shelf, so 250 g is 30000 cents.
    int m_weightProductId = 0;
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

    data::Database db(m_dbPath, data::DatabaseMode::Server);
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

    // A carton product: 24 pieces to the box, 60 cents a piece, so a carton is
    // 1440 cents. Sold by the carton, a line of 2 consumes 48 pieces.
    app::core::Product pack;
    pack.barcode = QStringLiteral("6130000000002");
    pack.name = QStringLiteral("علبة");
    pack.costPriceCents = 40;
    pack.salePriceCents = 60;
    pack.unit = QStringLiteral("قطعة");
    pack.packageSize = 24;
    pack.piecesPerPackage = 24;
    pack.packageBarcode = QStringLiteral("6130000000002c");
    pack.packageCostCents = 960;
    m_packageProductId = products.save(pack);
    QVERIFY(m_packageProductId > 0);
    app::core::StockMovement packStock;
    packStock.productId = m_packageProductId;
    packStock.delta = 100;
    packStock.reason = QStringLiteral("initial stock");
    packStock.createdAt = QDateTime::currentDateTimeUtc();
    QVERIFY(movements.insert(packStock) > 0);

    // A sold-by-weight product: 1200.00 the kilogram, with 100 kg (grams) on the
    // shelf. The shelf figure is in grams, the same unit a weight line carries.
    app::core::Product weighed;
    weighed.barcode = QStringLiteral("6130000000003");
    weighed.name = QStringLiteral("ميزان");
    weighed.costPriceCents = 80000;
    weighed.salePriceCents = 120000;
    weighed.unit = QStringLiteral("kg");
    weighed.soldByWeight = true;
    m_weightProductId = products.save(weighed);
    QVERIFY(m_weightProductId > 0);
    app::core::StockMovement weightStock;
    weightStock.productId = m_weightProductId;
    weightStock.delta = 100000;
    weightStock.reason = QStringLiteral("initial stock");
    weightStock.createdAt = QDateTime::currentDateTimeUtc();
    QVERIFY(movements.insert(weightStock) > 0);

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

QJsonObject SyncServerTest::saleOpWithUnit(int opId, const QByteArray& deviceId, int productId,
                                           long long quantity, long long unitPriceCents,
                                           const QString& unitKind, long long piecesConsumed,
                                           long long adjustmentCents)
{
    // The same shape a phone now sends: the line names its unit and the pieces
    // it consumes, and the operation carries the adjustment. quantity hits the
    // money math unchanged; piecesConsumed is what the receiving side checks
    // against the central product definition.
    QJsonObject item;
    item.insert(QStringLiteral("productId"), productId);
    item.insert(QStringLiteral("quantity"), QString::number(quantity));
    item.insert(QStringLiteral("unitPriceCents"), QString::number(unitPriceCents));
    item.insert(QStringLiteral("unitKind"), unitKind);
    item.insert(QStringLiteral("piecesConsumed"), QString::number(piecesConsumed));

    QJsonObject op;
    op.insert(QStringLiteral("opId"), opId);
    op.insert(QStringLiteral("type"), static_cast<int>(core::SyncOpType::Sale));
    op.insert(QStringLiteral("entityId"), 0);
    op.insert(QStringLiteral("amountCents"), QString::number(quantity * unitPriceCents + adjustmentCents));
    op.insert(QStringLiteral("adjustmentCents"), QString::number(adjustmentCents));
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
    op.insert(QStringLiteral("deviceId"), QString::fromUtf8(deviceId));
    op.insert(QStringLiteral("items"), QJsonArray{});
    return op;
}

void SyncServerTest::hmacValidProcessesBatch()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(saleOp(1, m_deviceId, m_productId, 1, 12000));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);

    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY2(result.hmacValid, qPrintable(QString("HMAC invalid: %1").arg(result.error)));
    QVERIFY2(result.batchValid, qPrintable(QString("Batch invalid: %1").arg(result.error)));
    QCOMPARE(result.applied.size(), 1);
    QVERIFY(result.errors.isEmpty());
    QCOMPARE(result.applied[0].type, QStringLiteral("sale"));
    QCOMPARE(result.applied[0].totalCents, 12000);
    QCOMPARE(result.applied[0].cogsCents, 9500);
}

void SyncServerTest::hmacInvalidRejected()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
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
    data::Database db(m_dbPath, data::DatabaseMode::Server);
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
    data::Database db(m_dbPath, data::DatabaseMode::Server);
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
    data::Database db(m_dbPath, data::DatabaseMode::Server);
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

void SyncServerTest::package_sale_lands_the_pieces_the_central_definition_says()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(saleOpWithUnit(101, m_deviceId, m_packageProductId, /*quantity=*/2,
                              /*unitPriceCents=*/1440, QStringLiteral("package"),
                              /*piecesConsumed=*/48, /*adjustmentCents=*/0));

    const network::SyncBatchResult result = processor.processJson(ops);
    QVERIFY2(result.errors.isEmpty(),
             qPrintable(result.errors.empty() ? QStringLiteral("no error")
                                              : result.errors.first().message));
    QCOMPARE(result.applied.size(), 1);
    QCOMPARE(result.applied[0].totalCents, 2880LL);

    // 48 pieces left the shelf, not 2: the carton was consumed at the count the
    // central product defines, not the count of cartons.
    data::StockMovementRepository movements(db);
    QCOMPARE(movements.sumByProductId(m_packageProductId), 100 - 48);

    data::SaleItemRepository saleItems(db);
    const std::vector<core::SaleItem> lines = saleItems.findBySaleId(result.applied[0].entityId);
    QCOMPARE(lines.size(), std::size_t(1));
    QCOMPARE(lines[0].unitKind, QStringLiteral("package"));
    QCOMPARE(lines[0].piecesConsumed, 48LL);
}

void SyncServerTest::weight_sale_lands_the_grams_the_central_definition_says()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(saleOpWithUnit(102, m_deviceId, m_weightProductId, /*quantity=*/250,
                              /*unitPriceCents=*/120000, QStringLiteral("weight"),
                              /*piecesConsumed=*/250, /*adjustmentCents=*/0));

    const network::SyncBatchResult result = processor.processJson(ops);
    QVERIFY2(result.errors.isEmpty(),
             qPrintable(result.errors.empty() ? QStringLiteral("no error")
                                              : result.errors.first().message));
    QCOMPARE(result.applied.size(), 1);
    // 1200.00 the kilogram for 250 g is a quarter of it, 300.00.
    QCOMPARE(result.applied[0].totalCents, 30000LL);

    data::StockMovementRepository movements(db);
    QCOMPARE(movements.sumByProductId(m_weightProductId), 100000 - 250);

    data::SaleItemRepository saleItems(db);
    const std::vector<core::SaleItem> lines = saleItems.findBySaleId(result.applied[0].entityId);
    QCOMPARE(lines.size(), std::size_t(1));
    QCOMPARE(lines[0].unitKind, QStringLiteral("weight"));
    QCOMPARE(lines[0].piecesConsumed, 250LL);
}

void SyncServerTest::debtAppliesWithoutCashSession()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
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
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(paymentOp(1, m_customerId, 3000, "دفعة", m_deviceId));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);

    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY2(!result.applied.empty(),
             qPrintable(QString("payment: applied empty, errors=%1 first=%2 hmacValid=%3 batchValid=%4")
                        .arg(result.errors.size())
                        .arg(result.errors.empty() ? QString("none") : result.errors[0].message)
                        .arg(result.hmacValid)
                        .arg(result.batchValid)));
    QVERIFY(result.hmacValid);
    QVERIFY(result.batchValid);
    QCOMPARE(result.applied.size(), 1);
    QCOMPARE(result.applied[0].type, QStringLiteral("customer_payment"));
    QCOMPARE(result.applied[0].totalCents, 3000);

    // Payment added +3000 to the open session cash.
    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 3000);
}

void SyncServerTest::zeroOpIdRejected()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    QJsonObject bad = saleOp(0, m_deviceId, m_productId, 1, 12000);
    ops.append(bad);

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);

    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY(result.hmacValid);
    QVERIFY(result.applied.isEmpty());
    QCOMPARE(result.errors.size(), 1);
    QVERIFY(result.errors[0].message.contains(QStringLiteral("opId")));

    // Nothing was applied, including via processJson.
    const network::SyncBatchResult direct = processor.processJson(ops);
    QVERIFY(direct.applied.isEmpty());
    QCOMPARE(direct.errors.size(), 1);
}

void SyncServerTest::emptyDeviceIdRejected()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(saleOp(1, QByteArray(), m_productId, 1, 12000));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);

    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY(result.hmacValid);
    QVERIFY(result.applied.isEmpty());
    QCOMPARE(result.errors.size(), 1);
    QVERIFY(result.errors[0].message.contains(QStringLiteral("deviceId")));

    const network::SyncBatchResult direct = processor.processJson(ops);
    QVERIFY(direct.applied.isEmpty());
    QCOMPARE(direct.errors.size(), 1);
}

void SyncServerTest::invalidLegacyNumberIsReturnedAsOperationError()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);
    QJsonObject op = paymentOp(71, m_customerId, 100, QByteArrayLiteral("bad amount"), m_deviceId);
    op.insert(QStringLiteral("amountCents"), 100.7);
    const network::SyncBatchResult result = processor.processJson(QJsonArray{op});
    QCOMPARE(result.errors.size(), 1);
    QCOMPARE(result.applied.size(), 0);
    QCOMPARE(result.errors.first().errorClass, network::SyncErrorClass::Permanent);
    QVERIFY2(result.errors.first().message.contains(QStringLiteral("fractional")),
             qPrintable(result.errors.first().message));
}

void SyncServerTest::sync_audit_actor_uses_device_id()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);
    QJsonArray ops;
    ops.append(saleOp(72, m_deviceId, m_productId, 1, 12000));
    ops.append(paymentOp(73, m_customerId, 1000, QByteArrayLiteral("payment"), m_deviceId));

    const network::SyncBatchResult result = processor.processJson(ops);
    QCOMPARE(result.applied.size(), 2);
    QVERIFY(result.errors.isEmpty());

    const auto entries = data::AuditLogRepository(db).findAll();
    QCOMPARE(entries.size(), std::size_t(2));
    for (const core::AuditLogEntry& entry : entries) {
        QCOMPARE(entry.actor, QString::fromUtf8(m_deviceId));
    }
}

void SyncServerTest::processJsonReplayAlreadyApplied()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);

    // Apply once via the wire path.
    QJsonArray first;
    first.append(debtOp(5, m_customerId, m_deviceId, m_productId, 1, 12000));
    const QByteArray body = QJsonDocument(first).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);
    const network::SyncBatchResult applied = processor.process(body, signature, m_key);
    QVERIFY(applied.hmacValid);
    QCOMPARE(applied.applied.size(), 1);
    QVERIFY(!applied.applied[0].alreadyApplied);

    // Replay the exact same op through processJson (test-only entry point):
    // must be reported alreadyApplied, not applied twice. This closes the
    // idempotency gap between the two application paths.
    const network::SyncBatchResult replay = processor.processJson(first);
    QVERIFY(replay.batchValid);
    QCOMPARE(replay.applied.size(), 1);
    QVERIFY(replay.applied[0].alreadyApplied);
    QCOMPARE(replay.applied[0].entityId, applied.applied[0].entityId);
    QCOMPARE(replay.applied[0].totalCents, 12000);

    // Total across both paths: exactly one customer transaction, one stock move.
    data::CustomerTransactionRepository txs(db);
    QCOMPARE(txs.findByCustomerId(m_customerId).size(), 1);
    data::StockMovementRepository movements(db);
    QCOMPARE(movements.sumByProductId(m_productId), 99);
}

void SyncServerTest::httpPostAppliesBatch()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
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

void SyncServerTest::duplicateBatchAppliedOnce()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);

    QJsonArray ops;
    ops.append(saleOp(1, m_deviceId, m_productId, 2, 12000));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);

    // First delivery: applied for real.
    const network::SyncBatchResult first = processor.process(body, signature, m_key);
    QVERIFY(first.hmacValid);
    QVERIFY(first.batchValid);
    QCOMPARE(first.applied.size(), 1);
    QVERIFY(!first.applied[0].alreadyApplied);

    const int originalEntityId = first.applied[0].entityId;

    // ACK lost on the device, so the same batch is re-sent with the same opId.
    const network::SyncBatchResult second = processor.process(body, signature, m_key);
    QVERIFY(second.hmacValid);
    QVERIFY(second.batchValid);
    QCOMPARE(second.applied.size(), 1);
    QVERIFY(second.applied[0].alreadyApplied);
    QCOMPARE(second.applied[0].entityId, originalEntityId);
    QCOMPARE(second.applied[0].totalCents, first.applied[0].totalCents);
    QCOMPARE(second.applied[0].cogsCents, first.applied[0].cogsCents);

    // Exactly-once: sale row exists exactly once, stock deducted only once.
    data::SaleRepository sales(db);
    QCOMPARE(sales.countByDeviceId(m_deviceId), 1);
    data::StockMovementRepository movements(db);
    QCOMPARE(movements.sumByProductId(m_productId), 100 - 2);
    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 24000);
}

void SyncServerTest::duplicateAcrossBatchAndReplayAppliesOnce()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);

    // op 1 then op 2 in one batch...
    QJsonArray firstBatch;
    firstBatch.append(saleOp(1, m_deviceId, m_productId, 1, 12000));
    firstBatch.append(paymentOp(2, m_customerId, 3000, "دفعة", m_deviceId));
    const QByteArray firstBody = QJsonDocument(firstBatch).toJson(QJsonDocument::Compact);
    const QByteArray firstSig = network::SyncProtocol::hmacSha256(firstBody, m_key);
    const network::SyncBatchResult first = processor.process(firstBody, firstSig, m_key);
    QVERIFY(first.hmacValid);
    QCOMPARE(first.applied.size(), 2);

    // ...the device only got the ACK for op 1 and replays op 2 alone.
    QJsonArray replay;
    replay.append(paymentOp(2, m_customerId, 3000, "دفعة", m_deviceId));
    const QByteArray replayBody = QJsonDocument(replay).toJson(QJsonDocument::Compact);
    const QByteArray replaySig = network::SyncProtocol::hmacSha256(replayBody, m_key);
    const network::SyncBatchResult replayResult = processor.process(replayBody, replaySig, m_key);
    QVERIFY(replayResult.hmacValid);
    QVERIFY(replayResult.batchValid);
    QCOMPARE(replayResult.applied.size(), 1);
    QVERIFY(replayResult.applied[0].alreadyApplied);
    QCOMPARE(replayResult.applied[0].entityId, first.applied[1].entityId);
    QCOMPARE(replayResult.applied[0].totalCents, 3000);

    // Exactly-once for the payment too: only one cash movement of +3000.
    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 15000);
}

void SyncServerTest::duplicateInsideSingleBatchAppliesOnce()
{
    data::Database db(m_dbPath, data::DatabaseMode::Server);
    network::SyncProcessor processor(db);

    // The SAME opId twice inside one batch: the second reference must be
    // deduplicated against the first within the same request.
    QJsonArray ops;
    ops.append(saleOp(9, m_deviceId, m_productId, 1, 12000));
    ops.append(saleOp(9, m_deviceId, m_productId, 1, 12000));

    const QByteArray body = QJsonDocument(ops).toJson(QJsonDocument::Compact);
    const QByteArray signature = network::SyncProtocol::hmacSha256(body, m_key);
    const network::SyncBatchResult result = processor.process(body, signature, m_key);
    QVERIFY(result.hmacValid);
    QCOMPARE(result.applied.size(), 2);

    QVERIFY(!result.applied[0].alreadyApplied);
    QVERIFY(result.applied[1].alreadyApplied);
    QCOMPARE(result.applied[1].entityId, result.applied[0].entityId);

    // Sale row exists exactly once; stock deducted exactly once.
    data::SaleRepository sales(db);
    QCOMPARE(sales.countByDeviceId(m_deviceId), 1);
    data::StockMovementRepository movements(db);
    QCOMPARE(movements.sumByProductId(m_productId), 99);
    data::CashMovementRepository cash(db);
    QCOMPARE(cash.sumBySessionId(m_openSessionId), 12000);
}

QTEST_GUILESS_MAIN(SyncServerTest)
#include "tst_sync_server.moc"