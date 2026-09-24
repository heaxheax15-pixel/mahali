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
    void reverse(int originalExpenseId);

private:
    Database& m_db;
};

} // namespace app::data