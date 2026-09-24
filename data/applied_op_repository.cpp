#include "applied_op_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

namespace {

const char* kAppliedColumns =
    "id, device_id, op_id, op_type, entity_id, total_cents, cogs_cents, applied_at";

core::AppliedOpRecord recordFromQuery(const QSqlQuery& query)
{
    core::AppliedOpRecord record;
    record.id = query.value(0).toInt();
    record.deviceId = query.value(1).toString();
    record.opId = query.value(2).toInt();
    record.opType = query.value(3).toInt();
    record.entityId = query.value(4).toInt();
    record.totalCents = query.value(5).toLongLong();
    record.cogsCents = query.value(6).toLongLong();
    record.appliedAt = fromIso(query.value(7).toString()).value_or(QDateTime());
    return record;
}

} // namespace

AppliedOpRepository::AppliedOpRepository(Database& db)
    : m_db(db)
{
}

std::optional<core::AppliedOpRecord> AppliedOpRepository::findByDeviceOp(const QString& deviceId,
                                                                         int opId) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM applied_ops WHERE device_id = ? AND op_id = ?")
                      .arg(QLatin1StringView(kAppliedColumns)));
    query.addBindValue(deviceId);
    query.addBindValue(opId);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return recordFromQuery(query);
}

int AppliedOpRepository::insert(const core::AppliedOpRecord& record)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO applied_ops (device_id, op_id, op_type, entity_id, "
                       "total_cents, cogs_cents, applied_at) VALUES (?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(record.deviceId);
    query.addBindValue(record.opId);
    query.addBindValue(record.opType);
    query.addBindValue(record.entityId);
    query.addBindValue(record.totalCents);
    query.addBindValue(record.cogsCents);
    query.addBindValue(toIso(record.appliedAt));
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

int AppliedOpRepository::count() const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM applied_ops"));
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

int AppliedOpRepository::pruneOlderThan(const QDateTime& cutoff, int maxRows)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "DELETE FROM applied_ops WHERE id IN ("
        "SELECT id FROM applied_ops WHERE applied_at < ? ORDER BY id LIMIT ?)"));
    query.addBindValue(toIso(cutoff));
    query.addBindValue(maxRows);
    if (!query.exec()) {
        return 0;
    }
    return query.numRowsAffected();
}

} // namespace app::data