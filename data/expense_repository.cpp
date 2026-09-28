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
        m_db.recordError(query.lastError(), QStringLiteral("ExpenseRepository::findBetween"));
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
    query.addBindValue(expense.label.isNull() ? QStringLiteral("") : expense.label);
    query.addBindValue(expense.amountCents);
    query.addBindValue(expense.reversedId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ExpenseRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool ExpenseRepository::reverse(int originalExpenseId)
{
    const std::optional<core::Expense> original = findById(originalExpenseId);
    if (!original.has_value()) {
        return false;
    }
    // A reversal carries the id of what it cancels, so reversing one would
    // cancel the cancellation. The service also refuses these on the amount
    // sign, but the repository refuses them on their own account rather than
    // relying on every caller to check.
    if (original->reversedId != 0 || original->amountCents <= 0) {
        return false;
    }

    // Nothing stops the same entry being reversed twice, and reversed_id has
    // no unique index, so the guard has to be a lookup. Without it a second
    // click writes a second mirrored row and returns the money to the till
    // twice.
    QSqlQuery existing(m_db.handle());
    existing.prepare(QStringLiteral("SELECT 1 FROM expenses WHERE reversed_id = ? LIMIT 1"));
    existing.addBindValue(originalExpenseId);
    if (!existing.exec()) {
        m_db.recordError(existing.lastError(), QStringLiteral("ExpenseRepository::reverse"));
        return false;
    }
    if (existing.next()) {
        return false;
    }

    core::Expense reversal;
    reversal.createdAt = QDateTime::currentDateTime();
    reversal.label = original->label;
    reversal.amountCents = -original->amountCents;
    reversal.reversedId = originalExpenseId;
    return insert(reversal) > 0;
}

} // namespace app::data