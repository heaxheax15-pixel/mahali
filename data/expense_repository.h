#pragma once

#include <QDateTime>
#include <optional>
#include <vector>

#include "core/expense.h"
#include "database.h"

namespace app::data {

class ExpenseRepository {
public:
    explicit ExpenseRepository(Database& db);

    std::optional<core::Expense> findById(int id) const;
    std::vector<core::Expense> findBetween(const QDateTime& from, const QDateTime& to) const;

    int insert(const core::Expense& expense);

    // Writes the mirrored row and reports whether it was written. Returns
    // false — changing nothing — when the original is gone, is itself a
    // reversal, or has already been reversed.
    //
    // No transaction is opened here: CashEntryService calls this inside one it
    // owns, because the mirrored row and the cash movement that returns the
    // money to the till have to land together or not at all. A caller with no
    // transaction of its own gets an uncommitted row, same as insert().
    bool reverse(int originalExpenseId);

private:
    Database& m_db;
};

} // namespace app::data