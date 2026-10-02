#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include "core/sync_operation_codec.h"
#include "network/sync_protocol.h"

class SyncTest : public QObject
{
    Q_OBJECT

private slots:
    void hmacSha256IsDeterministic();
    void hmacSha256DiffersAcrossKeys();
    void serializeDeserializeRoundTrip();
    void deserializeLegacyNumericAmount();
    void rejectInvalidLegacyNumericAmounts();
    void batchSizeLimits();
    void errorClassForStatus();
};

void SyncTest::hmacSha256IsDeterministic()
{
    const QByteArray payload = "1002|Sale|op=7,amt=1234";
    const QByteArray key = "secret";
    QCOMPARE(app::network::SyncProtocol::hmacSha256(payload, key),
            app::network::SyncProtocol::hmacSha256(payload, key));
    QVERIFY(!app::network::SyncProtocol::hmacSha256(payload, key).isEmpty());
}

void SyncTest::hmacSha256DiffersAcrossKeys()
{
    const QByteArray payload = "1002|Sale|op=7,amt=1234";
    QVERIFY(app::network::SyncProtocol::hmacSha256(payload, "key-a")
            != app::network::SyncProtocol::hmacSha256(payload, "key-b"));
}

void SyncTest::serializeDeserializeRoundTrip()
{
    app::core::SyncOperation op;
    op.opId = 42;
    op.type = app::core::SyncOpType::CustomerDebt;
    op.entityId = 7;
    op.amountCents = 123450;
    op.occurredAt = QDateTime::currentDateTimeUtc();
    op.note = QStringLiteral("debt note");
    op.deviceId = QStringLiteral("device-1");

    const QByteArray payload = app::network::SyncProtocol::serializeOp(op);
    QVERIFY(!payload.isEmpty());

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    QVERIFY(doc.isObject());
    const auto restored = app::network::SyncProtocol::deserializeOp(doc.object());
    QVERIFY(restored.has_value());
    QCOMPARE(restored->opId, 42);
    QCOMPARE(restored->type, app::core::SyncOpType::CustomerDebt);
    QCOMPARE(restored->entityId, 7);
    QCOMPARE(restored->amountCents, 123450);
    QCOMPARE(restored->occurredAt, op.occurredAt);
    QCOMPARE(restored->note, QStringLiteral("debt note"));
    QCOMPARE(restored->deviceId, QStringLiteral("device-1"));
}

void SyncTest::deserializeLegacyNumericAmount()
{
    QJsonObject legacy;
    legacy.insert(QStringLiteral("opId"), 1);
    legacy.insert(QStringLiteral("type"), static_cast<int>(app::core::SyncOpType::CustomerPayment));
    legacy.insert(QStringLiteral("entityId"), 2);
    legacy.insert(QStringLiteral("amountCents"), 9007199254740992.0);
    QString error;
    const auto restored = app::core::SyncOpCodec::deserialize(legacy, &error);
    QVERIFY2(restored.has_value(), qPrintable(error));
    QCOMPARE(restored->amountCents, app::core::SyncOpCodec::kMaxExactJsonNumber);
    QVERIFY(error.isEmpty());
}

void SyncTest::rejectInvalidLegacyNumericAmounts()
{
    QJsonObject legacy;
    legacy.insert(QStringLiteral("opId"), 1);
    legacy.insert(QStringLiteral("type"), static_cast<int>(app::core::SyncOpType::CustomerPayment));
    legacy.insert(QStringLiteral("entityId"), 2);

    const auto rejectedWithError = [&legacy](const QJsonValue& amount, const QString& expectedMessage) {
        legacy.insert(QStringLiteral("amountCents"), amount);
        QString error;
        const auto restored = app::core::SyncOpCodec::deserialize(legacy, &error);
        QVERIFY(!restored.has_value());
        QVERIFY2(!error.isEmpty(), "Rejected amount must expose a reason");
        QVERIFY2(error.contains(expectedMessage), qPrintable(error));
    };

    rejectedWithError(QJsonValue(100.7), QStringLiteral("fractional"));
    rejectedWithError(QJsonValue(1e300), QStringLiteral("range"));
    rejectedWithError(QJsonValue(QStringLiteral("not-a-number")), QStringLiteral("whole number"));
}

void SyncTest::batchSizeLimits()
{
    QVERIFY(!app::network::SyncProtocol::batchSizeValid(0));
    QVERIFY(app::network::SyncProtocol::batchSizeValid(1));
    QVERIFY(app::network::SyncProtocol::batchSizeValid(50));
    QVERIFY(!app::network::SyncProtocol::batchSizeValid(51));
}

void SyncTest::errorClassForStatus()
{
    using app::network::SyncErrorClass;
    using app::network::SyncProtocol;

    QCOMPARE(SyncProtocol::errorClassForStatus(200), SyncErrorClass::None);
    QCOMPARE(SyncProtocol::errorClassForStatus(400), SyncErrorClass::Permanent);
    QCOMPARE(SyncProtocol::errorClassForStatus(404), SyncErrorClass::Permanent);
    QCOMPARE(SyncProtocol::errorClassForStatus(500), SyncErrorClass::Retry);
    QCOMPARE(SyncProtocol::errorClassForStatus(503), SyncErrorClass::Retry);
    QCOMPARE(SyncProtocol::errorClassForStatus(0), SyncErrorClass::Network);
    QCOMPARE(SyncProtocol::errorClassForStatus(100), SyncErrorClass::Network);
}

QTEST_GUILESS_MAIN(SyncTest)
#include "tst_sync.moc"