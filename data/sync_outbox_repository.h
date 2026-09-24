#pragma once

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

private:
    Database& m_db;
};

} // namespace app::data