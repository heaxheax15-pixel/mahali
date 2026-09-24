#include "stock_movement_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

StockMovementRepository::StockMovementRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::StockMovement movementFromQuery(const QSqlQuery& query)
{
    core::StockMovement movement;
    movement.id = query.value(0).toInt();
    movement.productId = query.value(1).toInt();
    movement.delta = query.value(2).toLongLong();
    movement.reason = query.value(3).toString();
    movement.createdAt = fromIso(query.value(4).toString()).value_or(QDateTime());
    movement.reversedId = query.value(5).toInt();
    return movement;
}

const char* kMovementColumns = "id, product_id, delta, reason, created_at, reversed_id";

} // namespace

std::optional<core::StockMovement> StockMovementRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM stock_movements WHERE id = ?").arg(QLatin1StringView(kMovementColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return movementFromQuery(query);
}

std::vector<core::StockMovement> StockMovementRepository::findByProductId(int productId) const
{
    std::vector<core::StockMovement> movements;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM stock_movements WHERE product_id = ? ORDER BY created_at")
            .arg(QLatin1StringView(kMovementColumns)));
    query.addBindValue(productId);
    if (!query.exec()) {
        return movements;
    }
    while (query.next()) {
        movements.push_back(movementFromQuery(query));
    }
    return movements;
}

std::vector<core::StockMovement> StockMovementRepository::findBetween(const QDateTime& from,
                                                                      const QDateTime& to) const
{
    std::vector<core::StockMovement> movements;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM stock_movements WHERE created_at >= ? AND created_at <= ? ORDER BY created_at")
            .arg(QLatin1StringView(kMovementColumns)));
    query.addBindValue(toIso(from));
    query.addBindValue(toIso(to));
    if (!query.exec()) {
        return movements;
    }
    while (query.next()) {
        movements.push_back(movementFromQuery(query));
    }
    return movements;
}

long long StockMovementRepository::sumByProductId(int productId) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT COALESCE(SUM(delta), 0) FROM stock_movements WHERE product_id = ?"));
    query.addBindValue(productId);
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

int StockMovementRepository::insert(const core::StockMovement& movement)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO stock_movements (product_id, delta, reason, created_at, reversed_id) "
                       "VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(movement.productId);
    query.addBindValue(movement.delta);
    query.addBindValue(movement.reason);
    query.addBindValue(toIso(movement.createdAt.isValid() ? movement.createdAt : QDateTime::currentDateTime()));
    query.addBindValue(movement.reversedId);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

void StockMovementRepository::reverse(int originalMovementId, const QString& reason)
{
    const std::optional<core::StockMovement> original = findById(originalMovementId);
    if (!original.has_value()) {
        return;
    }
    core::StockMovement reversal;
    reversal.productId = original->productId;
    reversal.delta = -original->delta;
    reversal.reason = reason;
    reversal.createdAt = QDateTime::currentDateTime();
    reversal.reversedId = originalMovementId;
    insert(reversal);
}

} // namespace app::data