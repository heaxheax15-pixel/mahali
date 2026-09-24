#include "expense_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

ExpenseRepository::ExpenseRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::Expense expenseFromQuery(const QSqlQuery& query)
{
    core::Expense expense;
    expense.id = query.value(0).toInt();
    expense.createdAt = fromIso(query.value(1).toString()).value_or(QDateTime());
    expense.label = query.value(2).toString();
    expense.amountCents = query.value(3).toLongLong();
    expense.reversedId = query.value(4).toInt();
    return expense;
}

const char* kExpenseColumns = "id, created_at, label, amount_cents, reversed_id";

} // namespace

std::optional<core::Expense> ExpenseRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM expenses WHERE id = ?").arg(QLatin1StringView(kExpenseColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return expenseFromQuery(query);
}

std::vector<core::Expense> ExpenseRepository::findBetween(const QDateTime& from, const QDateTime& to) const
{
    std::vector<core::Expense> expenses;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM expenses WHERE created_at >= ? AND created_at <= ? ORDER BY created_at")
            .arg(QLatin1StringView(kExpenseColumns)));
    query.addBindValue(toIso(from));
    query.addBindValue(toIso(to));
    if (!query.exec()) {
        return expenses;
    }
    while (query.next()) {
        expenses.push_back(expenseFromQuery(query));
    }
    return expenses;
}

int ExpenseRepository::insert(const core::Expense& expense)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO expenses (created_at, label, amount_cents, reversed_id) VALUES (?, ?, ?, ?)"));
    query.addBindValue(toIso(expense.createdAt.isValid() ? expense.createdAt : QDateTime::currentDateTime()));
    query.addBindValue(expense.label);
    query.addBindValue(expense.amountCents);
    query.addBindValue(expense.reversedId);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

void ExpenseRepository::reverse(int originalExpenseId)
{
    const std::optional<core::Expense> original = findById(originalExpenseId);
    if (!original.has_value()) {
        return;
    }
    core::Expense reversal;
    reversal.createdAt = QDateTime::currentDateTime();
    reversal.label = original->label;
    reversal.amountCents = -original->amountCents;
    reversal.reversedId = originalExpenseId;
    insert(reversal);
}

} // namespace app::data