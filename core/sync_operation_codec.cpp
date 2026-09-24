#include "core/sync_operation_codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace app::core {

QByteArray SyncOpCodec::serialize(const SyncOperation& op)
{
    QJsonObject json;
    json[QStringLiteral("opId")] = op.opId;
    json[QStringLiteral("type")] = static_cast<int>(op.type);
    json[QStringLiteral("entityId")] = op.entityId;
    json[QStringLiteral("amountCents")] = QJsonValue(static_cast<double>(op.amountCents));
    json[QStringLiteral("occurredAt")] = op.occurredAt.toUTC().toString(Qt::ISODateWithMs);
    json[QStringLiteral("note")] = op.note;
    json[QStringLiteral("deviceId")] = op.deviceId;

    QJsonArray items;
    for (const SyncItem& item : op.items) {
        QJsonObject itemJson;
        itemJson[QStringLiteral("productId")] = item.productId;
        itemJson[QStringLiteral("quantity")] = QJsonValue(static_cast<double>(item.quantity));
        itemJson[QStringLiteral("unitPriceCents")] =
            QJsonValue(static_cast<double>(item.unitPriceCents));
        items.append(itemJson);
    }
    json[QStringLiteral("items")] = items;
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

std::optional<SyncOperation> SyncOpCodec::deserialize(const QJsonObject& json)
{
    if (!json.contains(QStringLiteral("opId")) || !json.contains(QStringLiteral("type"))
        || !json.contains(QStringLiteral("amountCents"))) {
        return std::nullopt;
    }

    SyncOperation op;
    op.opId = json.value(QStringLiteral("opId")).toInt();
    op.type = static_cast<SyncOpType>(json.value(QStringLiteral("type")).toInt());
    op.entityId = json.value(QStringLiteral("entityId")).toInt();
    op.amountCents =
        static_cast<long long>(json.value(QStringLiteral("amountCents")).toDouble());
    op.occurredAt =
        QDateTime::fromString(json.value(QStringLiteral("occurredAt")).toString(), Qt::ISODateWithMs);
    op.note = json.value(QStringLiteral("note")).toString();
    op.deviceId = json.value(QStringLiteral("deviceId")).toString();

    const QJsonArray items = json.value(QStringLiteral("items")).toArray();
    for (const QJsonValue& value : items) {
        if (!value.isObject()) {
            return std::nullopt;
        }
        const QJsonObject itemJson = value.toObject();
        SyncItem item;
        item.productId = itemJson.value(QStringLiteral("productId")).toInt();
        item.quantity = static_cast<long long>(itemJson.value(QStringLiteral("quantity")).toDouble());
        item.unitPriceCents =
            static_cast<long long>(itemJson.value(QStringLiteral("unitPriceCents")).toDouble());
        op.items.append(item);
    }
    return op;
}

} // namespace app::core