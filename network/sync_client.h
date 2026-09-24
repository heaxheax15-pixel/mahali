#pragma once

#include <QByteArray>
#include <QUrl>
#include <QVector>

#include "core/sync_operation.h"
#include "sync_protocol.h"

namespace app::network {

struct SyncSendResult {
    bool delivered = false;             // request reached the server (any HTTP status)
    SyncErrorClass errorClass = SyncErrorClass::Network;
    QString message;
    QVector<SyncAck> acks;              // per-operation outcomes
};

// Device-side sync client. Builds a batch from in-memory operations, signs it
// with HMAC-SHA256 (hex, matching SyncServer's expectations) and POSTs it to the
// server, translating the HTTP reply into the ACK taxonomy. Blocking to keep the
// orchestration logic trivial; callers run it off the UI thread.
class SyncClient {
public:
    static constexpr int kMaxBatchSize = 50;

    static SyncSendResult sendBatch(const QUrl& endpoint, const QByteArray& hmacKey,
                                    const QVector<app::core::SyncOperation>& ops);
};

} // namespace app::network