#pragma once

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

private:
    Database& m_db;
};

} // namespace app::data