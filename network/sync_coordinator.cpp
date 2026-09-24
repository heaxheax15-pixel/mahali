#include "sync_coordinator.h"

#include "data/sync_outbox_repository.h"

namespace app::network {

SyncCoordinator::SyncCoordinator(app::data::Database& db, const QUrl& endpoint, const QByteArray& hmacKey)
    : m_db(db)
    , m_endpoint(endpoint)
    , m_hmacKey(hmacKey)
{
}

int SyncCoordinator::pendingCount() const
{
    app::data::SyncOutboxRepository outbox(m_db);
    return outbox.countPending();
}

SyncDrainResult SyncCoordinator::drainOnce()
{
    SyncDrainResult result;

    app::data::SyncOutboxRepository outbox(m_db);
    const std::vector<app::core::SyncOutboxEntry> pending = outbox.findPending(SyncClient::kMaxBatchSize);
    if (pending.empty()) {
        result.errorClass = SyncErrorClass::None;
        return result;
    }
    result.attempted = true;

    QVector<app::core::SyncOperation> ops;
    ops.reserve(static_cast<qsizetype>(pending.size()));
    for (const app::core::SyncOutboxEntry& entry : pending) {
        ops.append(entry.op);
    }

    const SyncSendResult send = SyncClient::sendBatch(m_endpoint, m_hmacKey, ops);
    result.delivered = send.delivered;
    result.errorClass = send.errorClass;
    result.message = send.message;
    result.sent = ops.size();

    // The request never reached the server (or the server refused the batch as
    // a whole, e.g. invalid HMAC / malformed JSON). Without per-op ACKs we know
    // nothing about any single operation, so every one of them must stay pending
    // to be retried later — never thrown away.
    if (send.acks.isEmpty()) {
        for (const app::core::SyncOutboxEntry& entry : pending) {
            outbox.recordAttempt(entry.id);
        }
        result.keptPending = static_cast<int>(pending.size());
        return result;
    }

    // Reconcile ACKs back to the originating rows. ACKs are keyed by the
    // operation's opId; that opId is what the outbox row serialized.
    for (const app::core::SyncOutboxEntry& entry : pending) {
        const SyncAck* ack = nullptr;
        for (const SyncAck& candidate : send.acks) {
            if (candidate.appliedOpId == entry.op.opId) {
                ack = &candidate;
                break;
            }
        }
        if (ack == nullptr) {
            // The server did not mention this op (defensive); retry later.
            outbox.recordAttempt(entry.id);
            result.keptPending += 1;
            continue;
        }

        if (!ack->ok) {
            if (ack->errorClass == SyncErrorClass::Permanent) {
                outbox.markPermanentFailed(entry.id, ack->message);
                result.permanentFailed += 1;
            } else {
                // Retry (5xx) or network-level transient: keep for a later round.
                outbox.recordAttempt(entry.id);
                result.keptPending += 1;
            }
            continue;
        }

        outbox.markApplied(entry.id);
        result.applied += 1;
    }
    return result;
}

} // namespace app::network