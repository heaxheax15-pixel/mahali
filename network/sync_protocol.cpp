#include "sync_protocol.h"

#include <QCryptographicHash>
#include <QMessageAuthenticationCode>

#include "core/sync_operation_codec.h"

namespace app::network {

QByteArray SyncProtocol::hmacSha256(const QByteArray& payload, const QByteArray& key)
{
    return QMessageAuthenticationCode::hash(payload, key, QCryptographicHash::Sha256);
}

QByteArray SyncProtocol::serializeOp(const app::core::SyncOperation& op)
{
    return app::core::SyncOpCodec::serialize(op);
}

std::optional<app::core::SyncOperation> SyncProtocol::deserializeOp(const QJsonObject& json)
{
    return app::core::SyncOpCodec::deserialize(json);
}

bool SyncProtocol::batchSizeValid(int count)
{
    return count >= 1 && count <= 50;
}

SyncErrorClass SyncProtocol::errorClassForStatus(int httpStatus)
{
    if (httpStatus >= 100 && httpStatus <= 199) {
        // Informational only: no final status means the server never answered,
        // and re-sending will get the same non-answer.
        return SyncErrorClass::Permanent;
    }
    if (httpStatus >= 300 && httpStatus <= 399) {
        // Reached only when the client refused to follow the redirect (e.g. an
        // https->http downgrade); re-requesting the same URL hits the same
        // refusal.
        return SyncErrorClass::Permanent;
    }
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