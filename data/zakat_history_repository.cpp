#include "zakat_history_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

ZakatHistoryRepository::ZakatHistoryRepository(Database& db)
    : m_db(db)
{
}

namespace {

// Column order the queries select in, so the two readers cannot disagree about
// which number is which.
constexpr const char* kColumns =
    "id, year, nisab_cents, base_cents, due_cents, gold_price_cents, paid, paid_cents, paid_at";

ZakatHistory historyFromQuery(const QSqlQuery& query)
{
    ZakatHistory history;
    history.id = query.value(0).toInt();
    history.year = query.value(1).toInt();
    history.nisabCents = query.value(2).toLongLong();
    history.baseCents = query.value(3).toLongLong();
    history.dueCents = query.value(4).toLongLong();
    history.goldPriceCents = query.value(5).toLongLong();
    history.paid = query.value(6).toInt() != 0;
    // NULL means never paid, which is a different fact from paid at zero, so the
    // absence is collapsed to 0 and `paid` is what says which it was.
    history.paidCents = query.value(7).toLongLong();
    history.paidAt = query.value(8).toString();
    return history;
}

} // namespace

bool ZakatHistoryRepository::insertOrIgnore(int year, long long nisabCents, long long baseCents,
                                            long long dueCents, long long goldPriceCents)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO zakat_history "
        "(year, nisab_cents, base_cents, due_cents, gold_price_cents) VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(year);
    query.addBindValue(nisabCents);
    query.addBindValue(baseCents);
    query.addBindValue(dueCents);
    query.addBindValue(goldPriceCents);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ZakatHistoryRepository::insertOrIgnore"));
        return false;
    }
    return true;
}

bool ZakatHistoryRepository::markPaid(int year, long long paidCents, const QString& paidAt)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "UPDATE zakat_history SET paid = 1, paid_cents = ?, paid_at = ? WHERE year = ?"));
    query.addBindValue(paidCents);
    query.addBindValue(paidAt);
    query.addBindValue(year);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ZakatHistoryRepository::markPaid"));
        return false;
    }
    // No row matched means the year was never assessed. Returning false lets the
    // caller find out instead of believing a payment was recorded.
    return query.numRowsAffected() > 0;
}

std::optional<ZakatHistory> ZakatHistoryRepository::findByYear(int year) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM zakat_history WHERE year = ?")
                      .arg(QLatin1String(kColumns)));
    query.addBindValue(year);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return historyFromQuery(query);
}

std::vector<ZakatHistory> ZakatHistoryRepository::findAll() const
{
    std::vector<ZakatHistory> rows;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM zakat_history ORDER BY year DESC").arg(QLatin1String(kColumns)));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("ZakatHistoryRepository::findAll"));
        return rows;
    }
    while (query.next()) {
        rows.push_back(historyFromQuery(query));
    }
    return rows;
}

} // namespace app::data