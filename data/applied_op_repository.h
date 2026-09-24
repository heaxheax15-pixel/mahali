#pragma once

#include <QDateTime>
#include <optional>

#include "applied_op.h"
#include "database.h"

namespace app::data {

class AppliedOpRepository {
public:
    explicit AppliedOpRepository(Database& db);

    std::optional<core::AppliedOpRecord> findByDeviceOp(const QString& deviceId, int opId) const;

    // Returns the new row id, or 0 when the INSERT fails (e.g. a concurrent
    // duplicate hit the UNIQUE(device_id, op_id) constraint).
    int insert(const core::AppliedOpRecord& record);
    int count() const;

    // Deletes the exactly-once journal entries older than cutoff in bounded
    // batches. These rows only exist to deduplicate re-sent ops in-flight; once
    // they are a month old the outbox that could re-send them is long gone, so
    // pruning keeps the central file lean for years without weakening dedup of
    // anything that could actually arrive. Returns rows removed.
    int pruneOlderThan(const QDateTime& cutoff, int maxRows = 1000);

private:
    Database& m_db;
};

} // namespace app::data