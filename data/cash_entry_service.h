#pragma once

#include <QString>

#include "database.h"

namespace app::data {

struct CashEntryResult {
    bool ok = false;
    int entryId = 0;
    long long amountCents = 0;
    QString error;
};

// Records cash leaving the till (an expense or an owner drawing) atomically:
// the ledger row plus a matching negative cash movement on the open session,
// so the session's expected-till math stays correct.
class CashEntryService {
public:
    explicit CashEntryService(Database& db);

    CashEntryResult recordExpense(const QString& label, long long amountCents, int cashSessionId);
    CashEntryResult recordDrawing(const QString& note, long long amountCents, int cashSessionId);

    // Undoes a recorded entry: a reversed (negative) ledger row plus a positive
    // "refund" cash movement so the till returns to where it was.
    CashEntryResult reverseExpense(int expenseId, int cashSessionId);
    CashEntryResult reverseDrawing(int drawingId, int cashSessionId);

private:
    Database& m_db;
};

} // namespace app::data