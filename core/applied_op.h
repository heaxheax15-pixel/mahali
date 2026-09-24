#pragma once

#include <QDateTime>
#include <QString>

#include "core/sync_operation.h"

namespace app::core {

// Uniquely identifies a remote operation for exactly-once application.
// The (deviceId, opId) pair is the idempotency key; opId is a per-device counter.
struct SyncApplyToken {
    int opId = 0;
    QString deviceId = QStringLiteral("");
};

// A row of the server-side applied_ops journal. Written in the SAME transaction
// as the financial write it records, so an ACK loss / device retry can never
// double-apply money or stock on the Windows side.
struct AppliedOpRecord {
    int id = 0;
    int opId = 0;
    QString deviceId = QStringLiteral("");
    int opType = 0; // static_cast<int>(SyncOpType)
    int entityId = 0;
    long long totalCents = 0;
    long long cogsCents = 0;
    QDateTime appliedAt;
};

} // namespace app::core