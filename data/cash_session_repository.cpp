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

    // Validate before closing. Violations are reported but do not block the close.
    const auto violations = validateForClose(sessionId);
    if (!violations.isEmpty()) {
        qWarning() << "Cash session" << sessionId << "has" << violations.size() << "reconciliation violation(s):";
        for (const auto& v : violations) {
            qWarning() << "  [" << v.entityType << "#" << v.entityId << "] " << v.description;
        }
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

QVector<CashSessionViolation> CashSessionRepository::validateForClose(int sessionId) const
{
    QVector<CashSessionViolation> violations;

    // 1. Movements without a reference (ref_type or ref_id is NULL/0).
    // Old rows before the ref columns were added will have NULL/0.
    QSqlQuery unreferenced(m_db.handle());
    unreferenced.prepare(QStringLiteral(
        "SELECT id, type, amount_cents, ref_type, ref_id "
        "FROM cash_movements "
        "WHERE session_id = ? AND (ref_type IS NULL OR ref_type = '' OR ref_id IS NULL OR ref_id <= 0)"));
    unreferenced.addBindValue(sessionId);
    if (unreferenced.exec()) {
        while (unreferenced.next()) {
            CashSessionViolation v;
            v.description = QStringLiteral("حركة صندوق بدون مرجع مستند: النوع=%1 المبلغ=%2")
                                .arg(unreferenced.value(1).toString())
                                .arg(unreferenced.value(2).toLongLong());
            v.entityId = unreferenced.value(0).toInt();
            v.entityType = QStringLiteral("movement");
            violations.push_back(v);
        }
    }

    // 2. Non-voided sales: sum of their cash movements must equal sales.total_cents.
    // A sale is "non-voided" if sales.reversed_sale_id = 0.
    // Cash movements linked to a sale have ref_type = 'sale' and ref_id = sale_id.
    QSqlQuery sales(m_db.handle());
    sales.prepare(QStringLiteral(
        "SELECT s.id, s.total_cents, "
        "COALESCE(SUM(cm.amount_cents), 0) AS movement_sum "
        "FROM sales s "
        "JOIN cash_sessions cs ON cs.id = ? "
        "LEFT JOIN cash_movements cm ON cm.ref_type = 'sale' AND cm.ref_id = s.id "
        "WHERE s.reversed_sale_id = 0 AND ("
        "  (s.created_at >= cs.opened_at AND (cs.closed_at IS NULL OR s.created_at <= cs.closed_at)) "
        "  OR EXISTS (SELECT 1 FROM cash_movements own WHERE own.session_id = cs.id "
        "            AND own.ref_type = 'sale' AND own.ref_id = s.id)) "
        "GROUP BY s.id"));
    sales.addBindValue(sessionId);
    if (sales.exec()) {
        while (sales.next()) {
            const long long saleTotal = sales.value(1).toLongLong();
            const long long movementSum = sales.value(2).toLongLong();
            if (movementSum != saleTotal) {
                CashSessionViolation v;
                v.description = QStringLiteral("مجموع حركات البيع لا يساوي المجموع الكلي: البيع=%1 الحركات=%2")
                                    .arg(saleTotal)
                                    .arg(movementSum);
                v.entityId = sales.value(0).toInt();
                v.entityType = QStringLiteral("sale");
                violations.push_back(v);
            }
        }
    }

    // 3. Voided sales (reversed_sale_id != 0): net cash movements must sum to 0.
    // The reversal writes a negative movement, so original + reversal = 0.
    QSqlQuery voidedSales(m_db.handle());
    voidedSales.prepare(QStringLiteral(
        "SELECT s.id, COALESCE(SUM(cm.amount_cents), 0) AS net_sum "
        "FROM sales s "
        "JOIN cash_sessions cs ON cs.id = ? "
        "LEFT JOIN cash_movements cm ON cm.ref_type = 'sale' AND cm.ref_id = s.id "
        "WHERE s.reversed_sale_id != 0 AND ("
        "  (s.created_at >= cs.opened_at AND (cs.closed_at IS NULL OR s.created_at <= cs.closed_at)) "
        "  OR EXISTS (SELECT 1 FROM cash_movements own WHERE own.session_id = cs.id "
        "            AND own.ref_type = 'sale' AND own.ref_id = s.id)) "
        "GROUP BY s.id"));
    voidedSales.addBindValue(sessionId);
    if (voidedSales.exec()) {
        while (voidedSales.next()) {
            const long long netSum = voidedSales.value(1).toLongLong();
            if (netSum != 0) {
                CashSessionViolation v;
                v.description = QStringLiteral("صافي حركات البيع الملغى غير صفر: %1").arg(netSum);
                v.entityId = voidedSales.value(0).toInt();
                v.entityType = QStringLiteral("sale");
                violations.push_back(v);
            }
        }
    }

    // 4. No movement should reference a customer_transactions row (debts don't touch the till).
    QSqlQuery debtRefs(m_db.handle());
    debtRefs.prepare(QStringLiteral(
        "SELECT id, ref_type, ref_id FROM cash_movements "
        "WHERE session_id = ? AND ref_type IN ('customer_debt', 'customer_transaction', 'customer_transactions')"));
    debtRefs.addBindValue(sessionId);
    if (debtRefs.exec()) {
        while (debtRefs.next()) {
            CashSessionViolation v;
            v.description = QStringLiteral("حركة صندوق تشير إلى قيد دين (غير مسموح): المرجع=%1/%2")
                                .arg(debtRefs.value(1).toString())
                                .arg(debtRefs.value(2).toInt());
            v.entityId = debtRefs.value(0).toInt();
            v.entityType = QStringLiteral("movement");
            violations.push_back(v);
        }
    }

    return violations;
}

int CashSessionRepository::unreferencedMovementCount(int sessionId) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM cash_movements "
        "WHERE session_id = ? AND (ref_type IS NULL OR ref_type = '' OR ref_id IS NULL OR ref_id <= 0)"));
    query.addBindValue(sessionId);
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

} // namespace app::data