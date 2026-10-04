#include "customer_transaction_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

CustomerTransactionRepository::CustomerTransactionRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::CustomerTransaction txFromQuery(const QSqlQuery& query)
{
    core::CustomerTransaction tx;
    tx.id = query.value(0).toInt();
    tx.customerId = query.value(1).toInt();
    tx.amountCents = query.value(2).toLongLong();
    tx.adjustmentCents = query.value(5).toLongLong();
    tx.createdAt = fromIso(query.value(3).toString()).value_or(QDateTime());
    tx.reversedTransactionId = query.value(4).toInt();
    return tx;
}

// adjustment_cents comes last so the indices above keep their meaning: the column
// was added to the table after the rest of it, and every query in this file reads
// by position.
const char* kTxColumns =
    "id, customer_id, amount_cents, created_at, reversed_transaction_id, adjustment_cents";

} // namespace

std::optional<core::CustomerTransaction> CustomerTransactionRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM customer_transactions WHERE id = ?").arg(QLatin1StringView(kTxColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return txFromQuery(query);
}

std::vector<core::CustomerTransaction> CustomerTransactionRepository::findByCustomerId(int customerId) const
{
    std::vector<core::CustomerTransaction> txs;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM customer_transactions WHERE customer_id = ? ORDER BY created_at")
            .arg(QLatin1StringView(kTxColumns)));
    query.addBindValue(customerId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CustomerTransactionRepository::findByCustomerId"));
        return txs;
    }
    while (query.next()) {
        txs.push_back(txFromQuery(query));
    }
    return txs;
}

std::vector<core::CustomerTransaction> CustomerTransactionRepository::findBetween(const QDateTime& from,
                                                                                  const QDateTime& to) const
{
    std::vector<core::CustomerTransaction> txs;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM customer_transactions WHERE created_at >= ? AND created_at <= ? ORDER BY created_at")
            .arg(QLatin1StringView(kTxColumns)));
    query.addBindValue(toIso(from));
    query.addBindValue(toIso(to));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CustomerTransactionRepository::findBetween"));
        return txs;
    }
    while (query.next()) {
        txs.push_back(txFromQuery(query));
    }
    return txs;
}

int CustomerTransactionRepository::insert(const core::CustomerTransaction& transaction)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO customer_transactions (customer_id, amount_cents, created_at, reversed_transaction_id, adjustment_cents) "
                       "VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(transaction.customerId);
    query.addBindValue(transaction.amountCents);
    query.addBindValue(toIso(transaction.createdAt.isValid() ? transaction.createdAt : QDateTime::currentDateTime()));
    query.addBindValue(transaction.reversedTransactionId);
    query.addBindValue(transaction.adjustmentCents);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CustomerTransactionRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

} // namespace app::data