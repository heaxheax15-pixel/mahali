#include "cash_movement_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

CashMovementRepository::CashMovementRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::CashMovement movementFromQuery(const QSqlQuery& query)
{
    core::CashMovement movement;
    movement.id = query.value(0).toInt();
    movement.sessionId = query.value(1).toInt();
    movement.type = query.value(2).toString();
    movement.amountCents = query.value(3).toLongLong();
    movement.createdAt = fromIso(query.value(4).toString()).value_or(QDateTime());
    movement.note = query.value(5).toString();
    return movement;
}

} // namespace

std::vector<core::CashMovement> CashMovementRepository::findBySessionId(int sessionId) const
{
    std::vector<core::CashMovement> movements;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT id, session_id, type, amount_cents, created_at, note "
                       "FROM cash_movements WHERE session_id = ? ORDER BY created_at"));
    query.addBindValue(sessionId);
    if (!query.exec()) {
        return movements;
    }
    while (query.next()) {
        movements.push_back(movementFromQuery(query));
    }
    return movements;
}

long long CashMovementRepository::sumBySessionId(int sessionId) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT COALESCE(SUM(amount_cents), 0) FROM cash_movements WHERE session_id = ?"));
    query.addBindValue(sessionId);
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

int CashMovementRepository::insert(const core::CashMovement& movement)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO cash_movements (session_id, type, amount_cents, created_at, note) "
                       "VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(movement.sessionId);
    query.addBindValue(movement.type);
    query.addBindValue(movement.amountCents);
    query.addBindValue(toIso(movement.createdAt.isValid() ? movement.createdAt : QDateTime::currentDateTime()));
    query.addBindValue(movement.note);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

} // namespace app::data