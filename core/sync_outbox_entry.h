#pragma once

#include <QString>
#include <QVector>

#include "core/sync_operation.h"

namespace app::core {

enum class SyncOutboxStatus {
    Pending,        // not yet delivered / awaiting retry
    Applied,        // acknowledged by the server
    PermanentFailed // rejected by the server, do not retry
};

// A single locally-recorded sync operation waiting to be pushed to the server.
// Written in the offline-first data layer, read by the sync client.
struct SyncOutboxEntry {
    int id = 0;
    SyncOperation op;
    SyncOutboxStatus status = SyncOutboxStatus::Pending;
    int attempts = 0;
    QString lastError = QStringLiteral("");
};

} // namespace app::core