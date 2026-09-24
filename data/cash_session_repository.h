#pragma once

#include <optional>

#include "core/cash_session.h"
#include "database.h"

namespace app::data {

class CashSessionRepository {
public:
    explicit CashSessionRepository(Database& db);

    std::optional<core::CashSession> findById(int id) const;
    std::optional<core::CashSession> findOpen() const;

    int open(long long openingFloatCents);
    bool close(int sessionId, long long closingCountedCents, long long expectedCents, long long varianceCents);

private:
    Database& m_db;
};

} // namespace app::data