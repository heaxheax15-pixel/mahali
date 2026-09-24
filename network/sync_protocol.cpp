#include "sync_protocol.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QMessageAuthenticationCode>
#include <QCryptographicHash>

namespace app::network {

QByteArray SyncProtocol::hmacSha256(const QByteArray& payload, const QByteArray& key)
{
    return QMessageAuthenticationCode::hash(payload, key, QCryptographicHash::Sha256);
}

QByteArray SyncProtocol::serializeOp(const app::core::SyncOperation& op)
{
    QJsonObject json;
    json["opId"] = op.opId;
    json["type"] = static_cast<int>(op.type);
    json["entityId"] = op.entityId;
    json["amountCents"] = QJsonValue(static_cast<double>(op.amountCents));
    json["occurredAt"] = op.occurredAt.toUTC().toString(Qt::ISODateWithMs);
    json["note"] = op.note;
    json["deviceId"] = op.deviceId;
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

std::optional<app::core::SyncOperation> SyncProtocol::deserializeOp(const QJsonObject& json)
{
    if (!json.contains(QStringLiteral("opId")) || !json.contains(QStringLiteral("type"))
        || !json.contains(QStringLiteral("amountCents"))) {
        return std::nullopt;
    }

    app::core::SyncOperation op;
    op.opId = json.value(QStringLiteral("opId")).toInt();
    op.type = static_cast<app::core::SyncOpType>(json.value(QStringLiteral("type")).toInt());
    op.entityId = json.value(QStringLiteral("entityId")).toInt();
    op.amountCents = static_cast<long long>(json.value(QStringLiteral("amountCents")).toDouble());
    op.occurredAt = QDateTime::fromString(json.value(QStringLiteral("occurredAt")).toString(), Qt::ISODateWithMs);
    op.note = json.value(QStringLiteral("note")).toString();
    op.deviceId = json.value(QStringLiteral("deviceId")).toString();
    return op;
}

bool SyncProtocol::batchSizeValid(int count)
{
    return count >= 1 && count <= 50;
}

SyncErrorClass SyncProtocol::errorClassForStatus(int httpStatus)
{
    if (httpStatus >= 400 && httpStatus <= 499) {
        return SyncErrorClass::Permanent;
    }
    if (httpStatus >= 500 && httpStatus <= 599) {
        return SyncErrorClass::Retry;
    }
    if (httpStatus >= 200 && httpStatus <= 299) {
        return SyncErrorClass::None;
    }
    return SyncErrorClass::Network;
}

} // namespace app::network