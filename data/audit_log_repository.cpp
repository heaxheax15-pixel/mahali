#include "audit_log_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

AuditLogRepository::AuditLogRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::AuditLogEntry entryFromQuery(const QSqlQuery& query)
{
    core::AuditLogEntry entry;
    entry.id = query.value(0).toInt();
    entry.actor = query.value(1).toString();
    entry.action = query.value(2).toString();
    entry.target = query.value(3).toString();
    entry.createdAt = fromIso(query.value(4).toString()).value_or(QDateTime());
    return entry;
}

const char* kEntryColumns = "id, actor, action, target, created_at";

} // namespace

std::optional<core::AuditLogEntry> AuditLogRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM audit_log WHERE id = ?").arg(QLatin1StringView(kEntryColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return entryFromQuery(query);
}

std::vector<core::AuditLogEntry> AuditLogRepository::findBetween(const QDateTime& from,
                                                                 const QDateTime& to) const
{
    std::vector<core::AuditLogEntry> entries;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM audit_log WHERE created_at >= ? AND created_at <= ? ORDER BY created_at")
            .arg(QLatin1StringView(kEntryColumns)));
    query.addBindValue(toIso(from));
    query.addBindValue(toIso(to));
    if (!query.exec()) {
        return entries;
    }
    while (query.next()) {
        entries.push_back(entryFromQuery(query));
    }
    return entries;
}

std::vector<core::AuditLogEntry> AuditLogRepository::findAll() const
{
    std::vector<core::AuditLogEntry> entries;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM audit_log ORDER BY created_at").arg(QLatin1StringView(kEntryColumns)));
    if (!query.exec()) {
        return entries;
    }
    while (query.next()) {
        entries.push_back(entryFromQuery(query));
    }
    return entries;
}

int AuditLogRepository::insert(const core::AuditLogEntry& entry)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("INSERT INTO audit_log (actor, action, target, created_at) VALUES (?, ?, ?, ?)"));
    query.addBindValue(entry.actor);
    query.addBindValue(entry.action);
    query.addBindValue(entry.target);
    query.addBindValue(toIso(entry.createdAt.isValid() ? entry.createdAt : QDateTime::currentDateTime()));
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

} // namespace app::data