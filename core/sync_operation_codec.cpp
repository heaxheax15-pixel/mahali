#include "core/sync_operation_codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <cmath>

namespace app::core {
namespace {

void setError(QString* error, const QString& message)
{
    if (error != nullptr) {
        *error = message;
    }
}

} // namespace

QByteArray SyncOpCodec::serialize(const SyncOperation& op)
{
    QJsonObject json;
    json[QStringLiteral("opId")] = op.opId;
    json[QStringLiteral("type")] = static_cast<int>(op.type);
    json[QStringLiteral("entityId")] = op.entityId;
    // amountCents and quantity are long long (64-bit). JSON numbers are IEEE-754
    // doubles, which lose integer precision above 2^53 (~9e15). The max amount in
    // cents is 9,223,372,036,854,775,807, far above that limit, so we serialise
    // them as JSON strings and parse them back as long long on the other end.
    json[QStringLiteral("amountCents")] = QString::number(op.amountCents);
    json[QStringLiteral("occurredAt")] = op.occurredAt.toUTC().toString(Qt::ISODateWithMs);
    json[QStringLiteral("note")] = op.note;
    json[QStringLiteral("deviceId")] = op.deviceId;

    QJsonArray items;
    for (const SyncItem& item : op.items) {
        QJsonObject itemJson;
        itemJson[QStringLiteral("productId")] = item.productId;
        itemJson[QStringLiteral("quantity")] = QString::number(item.quantity);
        itemJson[QStringLiteral("unitPriceCents")] = QString::number(item.unitPriceCents);
        items.append(itemJson);
    }
    json[QStringLiteral("items")] = items;
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

bool SyncOpCodec::readExactInteger(const QJsonValue& value, const QString& fieldName, long long* out,
                                   QString* error)
{
    if (out == nullptr) {
        return false;
    }

    if (value.isString()) {
        // The current wire form: a decimal string holding the exact 64-bit
        // value, so the full range of a money amount survives the round trip.
        bool ok = false;
        const QString text = value.toString();
        const long long parsed = text.toLongLong(&ok);
        if (!ok) {
            setError(error, QStringLiteral("%1 is not a whole number: \"%2\"").arg(fieldName, text));
            return false;
        }
        *out = parsed;
        return true;
    }

    if (value.isDouble()) {
        // An older build wrote these as bare JSON numbers. A double holds every
        // integer exactly only up to 2^53 in magnitude, so anything outside
        // that range, and anything with a fractional part, is refused instead of
        // being rounded into a different amount of money.
        const double number = value.toDouble();
        if (!std::isfinite(number)) {
            setError(error, QStringLiteral("%1 is not a finite number").arg(fieldName));
            return false;
        }
        if (std::floor(number) != number) {
            setError(error,
                     QStringLiteral("%1 has a fractional part: %2").arg(fieldName, QString::number(number)));
            return false;
        }
        if (number < -static_cast<double>(kMaxExactJsonNumber)
            || number > static_cast<double>(kMaxExactJsonNumber)) {
            setError(error, QStringLiteral("%1 is outside the exactly representable range of +/-2^53: %2")
                                 .arg(fieldName, QString::number(number)));
            return false;
        }
        *out = static_cast<long long>(number);
        return true;
    }

    setError(error, QStringLiteral("%1 is missing or is neither a number nor a numeric string").arg(fieldName));
    return false;
}

std::optional<SyncOperation> SyncOpCodec::deserialize(const QJsonObject& json)
{
    return deserialize(json, nullptr);
}

std::optional<SyncOperation> SyncOpCodec::deserialize(const QJsonObject& json, QString* error)
{
    if (error != nullptr) {
        error->clear();
    }

    if (!json.contains(QStringLiteral("opId")) || !json.contains(QStringLiteral("type"))
        || !json.contains(QStringLiteral("amountCents"))) {
        setError(error, QStringLiteral("the operation is missing opId, type or amountCents"));
        return std::nullopt;
    }

    SyncOperation op;
    op.opId = json.value(QStringLiteral("opId")).toInt();
    op.type = static_cast<SyncOpType>(json.value(QStringLiteral("type")).toInt());
    op.entityId = json.value(QStringLiteral("entityId")).toInt();

    if (!readExactInteger(json.value(QStringLiteral("amountCents")), QStringLiteral("amountCents"),
                          &op.amountCents, error)) {
        return std::nullopt;
    }

    op.occurredAt =
        QDateTime::fromString(json.value(QStringLiteral("occurredAt")).toString(), Qt::ISODateWithMs);
    op.note = json.value(QStringLiteral("note")).toString();
    op.deviceId = json.value(QStringLiteral("deviceId")).toString();

    const QJsonArray items = json.value(QStringLiteral("items")).toArray();
    for (int index = 0; index < items.size(); ++index) {
        const QJsonValue value = items.at(index);
        if (!value.isObject()) {
            setError(error, QStringLiteral("item %1 is not a JSON object").arg(index));
            return std::nullopt;
        }
        const QJsonObject itemJson = value.toObject();
        SyncItem item;
        item.productId = itemJson.value(QStringLiteral("productId")).toInt();

        if (!readExactInteger(itemJson.value(QStringLiteral("quantity")),
                              QStringLiteral("quantity of item %1").arg(index), &item.quantity, error)) {
            return std::nullopt;
        }
        if (!readExactInteger(itemJson.value(QStringLiteral("unitPriceCents")),
                              QStringLiteral("unitPriceCents of item %1").arg(index), &item.unitPriceCents,
                              error)) {
            return std::nullopt;
        }
        op.items.append(item);
    }
    return op;
}

} // namespace app::core