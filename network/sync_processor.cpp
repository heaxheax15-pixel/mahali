#include "sync_processor.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "cash_session_repository.h"
#include "sync_protocol.h"

namespace app::network {

namespace {

QString opTypeString(app::core::SyncOpType type)
{
    switch (type) {
    case app::core::SyncOpType::Sale:
        return QStringLiteral("sale");
    case app::core::SyncOpType::CustomerDebt:
        return QStringLiteral("customer_debt");
    case app::core::SyncOpType::CustomerPayment:
        return QStringLiteral("customer_payment");
    }
    return QString();
}

// Returns a human-readable permanent error, or an empty string when valid.
QString validateOpIdentity(const QJsonObject& op)
{
    const QString deviceId = op.value(QStringLiteral("deviceId")).toString();
    const int opId = op.value(QStringLiteral("opId")).toInt();
    if (deviceId.isEmpty()) {
        return QStringLiteral("deviceId is required");
    }
    if (opId <= 0) {
        return QStringLiteral("opId must be a positive number");
    }
    return QString();
}

} // namespace

SyncProcessor::SyncProcessor(app::data::Database& db)
    : m_db(db)
    , m_sales(db)
    , m_payments(db)
    , m_appliedOps(db)
{
}

SyncBatchResult SyncProcessor::process(const QByteArray& body, const QByteArray& signature, const QByteArray& key)
{
    SyncBatchResult result;

    result.hmacValid = (signature == SyncProtocol::hmacSha256(body, key));
    if (!result.hmacValid) {
        result.error = QStringLiteral("invalid HMAC signature");
        return result;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isArray()) {
        result.error = QStringLiteral("batch must be a JSON array of operations");
        return result;
    }

    const QJsonArray ops = doc.array();
    result.batchValid = SyncProtocol::batchSizeValid(ops.size());
    if (!result.batchValid) {
        result.error = ops.isEmpty() ? QStringLiteral("empty batch")
                                     : QStringLiteral("batch exceeds 50 operations");
        return result;
    }

    for (const QJsonValue& value : ops) {
        if (!value.isObject()) {
            result.error = QStringLiteral("operation is not a JSON object");
            return result;
        }
        const QJsonObject op = value.toObject();

        // Permanent identity errors: without a non-empty deviceId and positive
        // opId the exactly-once journal key (device_id, op_id) is meaningless.
        const QString identityError = validateOpIdentity(op);
        if (!identityError.isEmpty()) {
            result.errors.append({ op.value(QStringLiteral("opId")).toInt(),
                                   SyncErrorClass::Permanent, identityError });
            continue;
        }

        // Exactly-once: if this (deviceId, opId) was already applied in a
        // previous batch whose ACK never reached the device, reply with the same
        // ACK instead of applying it again. Runs before the open-session gate so
        // a retry still succeeds even if the session has since closed.
        const QString opDeviceId = op.value(QStringLiteral("deviceId")).toString();
        const int opId = op.value(QStringLiteral("opId")).toInt();
        if (const auto existing = m_appliedOps.findByDeviceOp(opDeviceId, opId)) {
            SyncAppliedOp replayed;
            replayed.opId = opId;
            replayed.type = opTypeString(static_cast<app::core::SyncOpType>(existing->opType));
            replayed.entityId = existing->entityId;
            replayed.totalCents = existing->totalCents;
            replayed.cogsCents = existing->cogsCents;
            replayed.alreadyApplied = true;
            result.applied.append(replayed);
            continue;
        }

        QString error;
        SyncAppliedOp applied;
        const int type = op.value(QStringLiteral("type")).toInt();

        const app::data::CashSessionRepository sessions(m_db);
        const auto open = sessions.findOpen();

        switch (type) {
        case static_cast<int>(app::core::SyncOpType::Sale): {
            if (!open.has_value()) {
                result.errors.append({ op.value(QStringLiteral("opId")).toInt(),
                                       SyncErrorClass::Retry,
                                       QStringLiteral("no open cash session") });
                continue;
            }
            applied = applySale(op, open->id, &error);
            break;
        }
        case static_cast<int>(app::core::SyncOpType::CustomerDebt): {
            applied = applyDebt(op, &error);
            break;
        }
        case static_cast<int>(app::core::SyncOpType::CustomerPayment): {
            if (!open.has_value()) {
                result.errors.append({ op.value(QStringLiteral("opId")).toInt(),
                                       SyncErrorClass::Retry,
                                       QStringLiteral("no open cash session") });
                continue;
            }
            applied = applyPayment(op, open->id, &error);
            break;
        }
        default:
            error = QStringLiteral("unknown operation type %1").arg(type);
            break;
        }

        if (!error.isEmpty()) {
            result.errors.append({ applied.opId == 0 ? op.value(QStringLiteral("opId")).toInt()
                                                     : applied.opId,
                                   SyncErrorClass::Permanent, error });
        } else {
            result.applied.append(applied);
        }
    }
    return result;
}

SyncBatchResult SyncProcessor::processJson(const QJsonArray& ops)
{
    SyncBatchResult result;
    result.hmacValid = true;
    result.batchValid = SyncProtocol::batchSizeValid(ops.size());
    if (!result.batchValid) {
        result.error = ops.isEmpty() ? QStringLiteral("empty batch")
                                     : QStringLiteral("batch exceeds 50 operations");
        return result;
    }

    for (const QJsonValue& value : ops) {
        if (!value.isObject()) {
            result.errors.append({ 0, SyncErrorClass::Permanent,
                                   QStringLiteral("operation is not a JSON object") });
            continue;
        }
        const QJsonObject op = value.toObject();

        const QString identityError = validateOpIdentity(op);
        if (!identityError.isEmpty()) {
            result.errors.append({ op.value(QStringLiteral("opId")).toInt(),
                                   SyncErrorClass::Permanent, identityError });
            continue;
        }

        const QString opDeviceId = op.value(QStringLiteral("deviceId")).toString();
        const int opId = op.value(QStringLiteral("opId")).toInt();
        if (const auto existing = m_appliedOps.findByDeviceOp(opDeviceId, opId)) {
            SyncAppliedOp replayed;
            replayed.opId = opId;
            replayed.type = opTypeString(static_cast<app::core::SyncOpType>(existing->opType));
            replayed.entityId = existing->entityId;
            replayed.totalCents = existing->totalCents;
            replayed.cogsCents = existing->cogsCents;
            replayed.alreadyApplied = true;
            result.applied.append(replayed);
            continue;
        }

        QString error;
        SyncAppliedOp applied;
        const int type = op.value(QStringLiteral("type")).toInt();

        const app::data::CashSessionRepository sessions(m_db);
        const auto open = sessions.findOpen();

        switch (type) {
        case static_cast<int>(app::core::SyncOpType::Sale): {
            if (!open.has_value()) {
                result.errors.append({ opId, SyncErrorClass::Retry,
                                       QStringLiteral("no open cash session") });
                continue;
            }
            applied = applySale(op, open->id, &error);
            break;
        }
        case static_cast<int>(app::core::SyncOpType::CustomerDebt): {
            applied = applyDebt(op, &error);
            break;
        }
        case static_cast<int>(app::core::SyncOpType::CustomerPayment): {
            if (!open.has_value()) {
                result.errors.append({ opId, SyncErrorClass::Retry,
                                       QStringLiteral("no open cash session") });
                continue;
            }
            applied = applyPayment(op, open->id, &error);
            break;
        }
        default:
            error = QStringLiteral("unknown operation type %1").arg(type);
            break;
        }
        if (!error.isEmpty()) {
            result.errors.append({ applied.opId == 0 ? opId : applied.opId,
                                   SyncErrorClass::Permanent, error });
        } else {
            result.applied.append(applied);
        }
    }
    return result;
}

SyncAppliedOp SyncProcessor::applySale(const QJsonObject& json, int cashSessionId, QString* error)
{
    SyncAppliedOp applied;
    applied.opId = json.value(QStringLiteral("opId")).toInt();
    applied.type = QStringLiteral("sale");

    const QJsonArray itemsJson = json.value(QStringLiteral("items")).toArray();
    if (itemsJson.isEmpty()) {
        *error = QStringLiteral("sale has no items");
        return applied;
    }

    QVector<app::core::SaleItem> items;
    for (const QJsonValue& value : itemsJson) {
        const QJsonObject itemJson = value.toObject();
        app::core::SaleItem item;
        item.productId = itemJson.value(QStringLiteral("productId")).toInt();
        item.quantity = static_cast<long long>(itemJson.value(QStringLiteral("quantity")).toDouble());
        item.unitPriceCents =
            static_cast<long long>(itemJson.value(QStringLiteral("unitPriceCents")).toDouble());
        items.append(item);
    }

    const QString deviceId = json.value(QStringLiteral("deviceId")).toString();
    app::core::SyncApplyToken token;
    token.opId = applied.opId;
    token.deviceId = deviceId;
    const app::data::SaleRecordResult result =
        m_sales.recordSale(items, cashSessionId, deviceId, /*allowOversold=*/false, &token);
    if (!result.ok) {
        *error = result.error;
        return applied;
    }
    applied.entityId = result.saleId;
    applied.totalCents = result.totalCents;
    applied.cogsCents = result.cogsCents;
    applied.alreadyApplied = result.alreadyApplied;
    return applied;
}

SyncAppliedOp SyncProcessor::applyDebt(const QJsonObject& json, QString* error)
{
    SyncAppliedOp applied;
    applied.opId = json.value(QStringLiteral("opId")).toInt();
    applied.type = QStringLiteral("customer_debt");

    const int customerId = json.value(QStringLiteral("entityId")).toInt();
    if (customerId <= 0) {
        *error = QStringLiteral("customer_debt requires a valid customer id");
        return applied;
    }

    const QJsonArray itemsJson = json.value(QStringLiteral("items")).toArray();
    if (itemsJson.isEmpty()) {
        *error = QStringLiteral("customer_debt has no items");
        return applied;
    }

    QVector<app::core::SaleItem> items;
    for (const QJsonValue& value : itemsJson) {
        const QJsonObject itemJson = value.toObject();
        app::core::SaleItem item;
        item.productId = itemJson.value(QStringLiteral("productId")).toInt();
        item.quantity = static_cast<long long>(itemJson.value(QStringLiteral("quantity")).toDouble());
        item.unitPriceCents =
            static_cast<long long>(itemJson.value(QStringLiteral("unitPriceCents")).toDouble());
        items.append(item);
    }

    const QString deviceId = json.value(QStringLiteral("deviceId")).toString();
    app::core::SyncApplyToken token;
    token.opId = applied.opId;
    token.deviceId = deviceId;
    const app::data::SaleRecordResult result =
        m_sales.recordCustomerDebt(customerId, items, deviceId, /*allowOversold=*/false, &token);
    if (!result.ok) {
        *error = result.error;
        return applied;
    }
    applied.entityId = result.saleId;
    applied.totalCents = result.totalCents;
    applied.cogsCents = result.cogsCents;
    applied.alreadyApplied = result.alreadyApplied;
    return applied;
}

SyncAppliedOp SyncProcessor::applyPayment(const QJsonObject& json, int cashSessionId, QString* error)
{
    SyncAppliedOp applied;
    applied.opId = json.value(QStringLiteral("opId")).toInt();
    applied.type = QStringLiteral("customer_payment");

    const int customerId = json.value(QStringLiteral("entityId")).toInt();
    const long long amountCents =
        static_cast<long long>(json.value(QStringLiteral("amountCents")).toDouble());
    const QString note = json.value(QStringLiteral("note")).toString();

    const QString deviceId = json.value(QStringLiteral("deviceId")).toString();
    app::core::SyncApplyToken token;
    token.opId = applied.opId;
    token.deviceId = deviceId;
    const app::data::PaymentResult result =
        m_payments.recordCustomerPayment(customerId, amountCents, cashSessionId, note, &token);
    if (!result.ok) {
        *error = result.error;
        return applied;
    }
    applied.entityId = result.paymentId;
    applied.totalCents = result.amountCents;
    applied.alreadyApplied = result.alreadyApplied;
    return applied;
}

} // namespace app::network