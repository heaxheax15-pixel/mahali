#include "cash_session_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

CashSessionRepository::CashSessionRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::CashSession sessionFromQuery(const QSqlQuery& query)
{
    core::CashSession session;
    session.id = query.value(0).toInt();
    session.openedAt = fromIso(query.value(1).toString()).value_or(QDateTime());
    session.openingFloatCents = query.value(2).toLongLong();
    session.closedAt = fromIso(query.value(3).toString()).value_or(QDateTime());
    session.closingCountedCents = toLongLong(query.value(4)).value_or(0);
    session.expectedCents = toLongLong(query.value(5)).value_or(0);
    session.varianceCents = toLongLong(query.value(6)).value_or(0);
    session.status = query.value(7).toString();
    return session;
}

const char* kSessionColumns =
    "id, opened_at, opening_float_cents, closed_at, closing_counted_cents, expected_cents, variance_cents, status";

} // namespace

std::optional<core::CashSession> CashSessionRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM cash_sessions WHERE id = ?").arg(QLatin1StringView(kSessionColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return sessionFromQuery(query);
}

std::optional<core::CashSession> CashSessionRepository::findOpen() const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM cash_sessions WHERE status = 'open' LIMIT 1")
                      .arg(QLatin1StringView(kSessionColumns)));
    if (!query.exec()) {
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return sessionFromQuery(query);
}

int CashSessionRepository::open(long long openingFloatCents)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO cash_sessions (opened_at, opening_float_cents, status) VALUES (?, ?, 'open')"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    query.addBindValue(openingFloatCents);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool CashSessionRepository::close(int sessionId, long long closingCountedCents, long long expectedCents,
                                  long long varianceCents)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("UPDATE cash_sessions SET closed_at = ?, closing_counted_cents = ?, expected_cents = ?, "
                       "variance_cents = ?, status = 'closed' WHERE id = ?"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    query.addBindValue(closingCountedCents);
    query.addBindValue(expectedCents);
    query.addBindValue(varianceCents);
    query.addBindValue(sessionId);
    return query.exec();
}

} // namespace app::data