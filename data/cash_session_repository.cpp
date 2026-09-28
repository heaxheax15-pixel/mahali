#include "cash_session_repository.h"

#include <QDebug>
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

std::optional<core::CashSession> CashSessionRepository::findForDay(const QString& dayIso) const
{
    QSqlQuery query(m_db.handle());
    // A prefix match on the stored ISO text, so the day is read off the date part
    // without the caller having to say when the day starts and ends.
    query.prepare(QStringLiteral("SELECT %1 FROM cash_sessions WHERE opened_at LIKE ? ORDER BY opened_at DESC LIMIT 1")
                      .arg(QLatin1StringView(kSessionColumns)));
    query.addBindValue(dayIso + QStringLiteral("T%"));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CashSessionRepository::findForDay"));
        return std::nullopt;
    }
    if (!query.next()) {
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
        m_db.recordError(query.lastError(), QStringLiteral("CashSessionRepository::findOpen"));
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return sessionFromQuery(query);
}

int CashSessionRepository::open(long long openingFloatCents)
{
    // A session is opened with what is actually in the till. Zero or negative
    // would start a day that no count could ever reconcile, and once closed the
    // nonsense float would read as an ordinary deficit. The guard is here
    // rather than in the caller because every caller is a UI slot that someone
    // can reach with a stray value.
    if (openingFloatCents <= 0) {
        qWarning() << "cash session refused: non-positive opening float" << openingFloatCents;
        return 0;
    }

    // At most one session may be open at a time. findOpen() carries no ORDER BY,
    // so a second open row would leave the "current" session a coin toss and
    // strand every movement booked against the other one. findOpen() is reused
    // rather than a second count query so that this check cannot drift away from
    // the condition the rest of the app reads a session by.
    if (findOpen().has_value()) {
        qWarning() << "cash session refused: a session is already open";
        return 0;
    }

    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO cash_sessions (opened_at, opening_float_cents, status) VALUES (?, ?, 'open')"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    query.addBindValue(openingFloatCents);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CashSessionRepository::open"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool CashSessionRepository::close(int sessionId, long long closingCountedCents, long long expectedCents,
                                  long long varianceCents)
{
    // Only the counted amount is guarded. expectedCents and varianceCents are
    // derived, and a negative variance is a real result: a till that came up
    // short. A non-positive count is not — it means nothing was counted, and
    // accepting it would close the day with a phantom deficit the size of the
    // whole float, irreversibly.
    if (closingCountedCents <= 0) {
        qWarning() << "cash session refused: non-positive counted amount" << closingCountedCents;
        return false;
    }

    // status = 'open' in the WHERE clause is what makes a second close of the
    // same session report failure instead of rewriting a day that is already
    // settled.
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("UPDATE cash_sessions SET closed_at = ?, closing_counted_cents = ?, expected_cents = ?, "
                       "variance_cents = ?, status = 'closed' WHERE id = ? AND status = 'open'"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    query.addBindValue(closingCountedCents);
    query.addBindValue(expectedCents);
    query.addBindValue(varianceCents);
    query.addBindValue(sessionId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CashSessionRepository::close"));
        return false;
    }
    // exec() reports only that the statement ran. An UPDATE that matches no row
    // is still a success as far as SQLite is concerned, so this used to report
    // that it had closed a session id that never existed, and the caller treated
    // the day as settled.
    if (query.numRowsAffected() <= 0) {
        qWarning() << "cash session close matched no row for id" << sessionId;
        return false;
    }
    return true;
}

} // namespace app::data