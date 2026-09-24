#include "supplier_transaction_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

SupplierTransactionRepository::SupplierTransactionRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::SupplierTransaction txFromQuery(const QSqlQuery& query)
{
    core::SupplierTransaction tx;
    tx.id = query.value(0).toInt();
    tx.supplierId = query.value(1).toInt();
    tx.amountCents = query.value(2).toLongLong();
    tx.createdAt = fromIso(query.value(3).toString()).value_or(QDateTime());
    tx.note = query.value(4).toString();
    return tx;
}

const char* kTxColumns = "id, supplier_id, amount_cents, created_at, note";

} // namespace

std::optional<core::SupplierTransaction> SupplierTransactionRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM supplier_transactions WHERE id = ?").arg(QLatin1StringView(kTxColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return txFromQuery(query);
}

std::vector<core::SupplierTransaction> SupplierTransactionRepository::findBySupplierId(int supplierId) const
{
    std::vector<core::SupplierTransaction> txs;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM supplier_transactions WHERE supplier_id = ? ORDER BY created_at")
            .arg(QLatin1StringView(kTxColumns)));
    query.addBindValue(supplierId);
    if (!query.exec()) {
        return txs;
    }
    while (query.next()) {
        txs.push_back(txFromQuery(query));
    }
    return txs;
}

std::vector<core::SupplierTransaction> SupplierTransactionRepository::findBetween(const QDateTime& from,
                                                                                  const QDateTime& to) const
{
    std::vector<core::SupplierTransaction> txs;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM supplier_transactions WHERE created_at >= ? AND created_at <= ? ORDER BY created_at")
            .arg(QLatin1StringView(kTxColumns)));
    query.addBindValue(toIso(from));
    query.addBindValue(toIso(to));
    if (!query.exec()) {
        return txs;
    }
    while (query.next()) {
        txs.push_back(txFromQuery(query));
    }
    return txs;
}

int SupplierTransactionRepository::insert(const core::SupplierTransaction& transaction)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO supplier_transactions (supplier_id, amount_cents, created_at, note) "
                       "VALUES (?, ?, ?, ?)"));
    query.addBindValue(transaction.supplierId);
    query.addBindValue(transaction.amountCents);
    query.addBindValue(toIso(transaction.createdAt.isValid() ? transaction.createdAt : QDateTime::currentDateTime()));
    query.addBindValue(transaction.note.isNull() ? QStringLiteral("") : transaction.note);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

} // namespace app::data