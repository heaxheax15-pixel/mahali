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

    // Both refuse a non-positive amount: a session opened with a zero or
    // negative float can never be reconciled by counting, and closing on a
    // non-positive count would book the whole float as a phantom deficit.
    // expectedCents and varianceCents are not guarded, because a negative
    // variance is a real result — a till that came up short.
    int open(long long openingFloatCents);
    bool close(int sessionId, long long closingCountedCents, long long expectedCents, long long varianceCents);

private:
    Database& m_db;
};

} // namespace app::data