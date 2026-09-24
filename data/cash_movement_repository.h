#pragma once

#include <optional>
#include <vector>

#include "core/cash_movement.h"
#include "database.h"

namespace app::data {

class CashMovementRepository {
public:
    explicit CashMovementRepository(Database& db);

    std::vector<core::CashMovement> findBySessionId(int sessionId) const;
    long long sumBySessionId(int sessionId) const;

    int insert(const core::CashMovement& movement);

private:
    Database& m_db;
};

} // namespace app::data