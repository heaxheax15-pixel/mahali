#pragma once

#include <QDateTime>
#include <QString>
#include <optional>
#include <vector>

#include "core/sync_outbox_entry.h"
#include "database.h"

namespace app::data {

// Offline-first pending-op journal. Operations are written here on the device,
// then drained to the server in batches by the sync client. Status is updated
// in place (never deleted), so a crash-safe retry is trivial.
class SyncOutboxRepository {
public:
    explicit SyncOutboxRepository(Database& db);

    int enqueue(const core::SyncOperation& op);
    std::optional<core::SyncOutboxEntry> findById(int id) const;
    std::vector<core::SyncOutboxEntry> findPending(int limit) const;
    int countPending() const;

    bool markApplied(int id);
    bool markPermanentFailed(int id, const QString& error);

    // Bumps the delivery-attempt counter so the operator can see how many LAN
    // tries each still-pending operation has survived.
    bool recordAttempt(int id);

    // Deletes rows the server already acknowledged (status = 'applied') older
    // than cutoff, in bounded batches so a years-long cleanup pass never blocks
    // a live write. Pending rows are of course never touched, and permanently
    // failed rows are kept too: they are money the server refused, exactly the
    // anomalies an owner must still see and act on. Returns rows removed.
    int pruneAppliedOlderThan(const QDateTime& cutoff, int maxRows = 1000);

private:
    Database& m_db;
};

} // namespace app::data