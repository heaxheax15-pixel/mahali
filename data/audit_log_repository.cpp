#include "audit_log_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "core/session.h"
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
        m_db.recordError(query.lastError(), QStringLiteral("AuditLogRepository::findBetween"));
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
        m_db.recordError(query.lastError(), QStringLiteral("AuditLogRepository::findAll"));
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
        m_db.recordError(query.lastError(), QStringLiteral("AuditLogRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

int AuditLogRepository::record(const QString& action, const QString& target)
{
    // The actor is who is signed in here and now, read at the moment of writing
    // rather than passed in: a service has no business asking its caller who they
    // are, and a caller that got it wrong would put someone else's name on a
    // movement they did not make.
    return recordAs(core::Session::instance().actorName(), action, target);
}

int AuditLogRepository::recordAs(const QString& actor, const QString& action, const QString& target)
{
    core::AuditLogEntry entry;
    entry.actor = actor;
    entry.action = action;
    entry.target = target;
    entry.createdAt = QDateTime::currentDateTime();
    return insert(entry);
}

} // namespace app::data