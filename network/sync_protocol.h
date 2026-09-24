#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <optional>

#include "core/sync_operation.h"

namespace app::network {

enum class SyncErrorClass { None, Permanent, Retry, Network };

struct SyncAck {
    bool ok = false;
    int appliedOpId = 0;
    SyncErrorClass errorClass = SyncErrorClass::None;
    QString message;
};

// Appends a single HMAC-SHA256 signature to a payload so a subsequent local
// HTTP layer can verify authenticity before scheduling batched writes.
class SyncProtocol {
public:
    static QByteArray hmacSha256(const QByteArray& payload, const QByteArray& key);
    static QByteArray serializeOp(const app::core::SyncOperation& op);
    static std::optional<app::core::SyncOperation> deserializeOp(const QJsonObject& json);

    // Batches are capped at 50 operations and must be non-empty.
    static bool batchSizeValid(int count);
    // HTTP status taxa: 4xx permanent, 5xx retryable, 2xx none, else network.
    static SyncErrorClass errorClassForStatus(int httpStatus);
};

} // namespace app::network