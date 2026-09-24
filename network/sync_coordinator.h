#pragma once

#include <QByteArray>
#include <QUrl>

#include "data/database.h"
#include "sync_client.h"

namespace app::network {

struct SyncDrainResult {
    bool attempted = false;    // there was pending work to push
    bool delivered = false;    // request reached the server (any HTTP status)
    SyncErrorClass errorClass = SyncErrorClass::Network;
    int sent = 0;              // ops included in this round's batch
    int applied = 0;           // acked ok (or already-applied) by the server
    int permanentFailed = 0;   // rejected and marked permanent_failed
    int keptPending = 0;       // left pending for a later retry
    QString message;
};

// Device-side orchestration: the missing link between the offline journal and
// the wire. Each call reads the next pending batch (<= 50 ops) from the outbox,
// ships it with SyncClient, then reconciles ACKs back onto the very rows they
// came from. Exactly-once is preserved end to end: ops acked by the server are
// marked applied, ops the server rejects are canned, and ops that never reached
// the server (network/5xx) are left pending to retry later — a power cut at any
// moment simply resumes on the next drain.
class SyncCoordinator {
public:
    // Constructor injection: the device-side database owns the outbox journal.
    SyncCoordinator(app::data::Database& db, const QUrl& endpoint, const QByteArray& hmacKey);

    // Sends ONE batch (up to kMaxBatchSize) and reconciles its ACKs. Call this
    // repeatedly until pendingCount() == 0 or drainOnce() reports no progress.
    SyncDrainResult drainOnce();

    int pendingCount() const;

private:
    app::data::Database& m_db;
    QUrl m_endpoint;
    QByteArray m_hmacKey;
};

} // namespace app::network