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

    // The session opened on the given day, given as "YYYY-MM-DD". Opened on, not
    // active during: a session that runs past midnight belongs to the day it was
    // opened in, so a day's report and its till reconcile against each other. The
    // newest is returned when a day somehow has more than one, since the last one
    // opened is the one the operator is working in.
    std::optional<core::CashSession> findForDay(const QString& dayIso) const;

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