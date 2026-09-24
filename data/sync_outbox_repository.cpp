#include "sync_outbox_repository.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QSqlQuery>
#include <QVariant>

#include "core/sync_operation_codec.h"
#include "date_utils.h"

namespace app::data {

namespace {

const char* kOutboxColumns = "id, op_json, status, attempts, last_error";

core::SyncOutboxStatus statusFromString(const QString& value)
{
    if (value == QStringLiteral("applied")) {
        return core::SyncOutboxStatus::Applied;
    }
    if (value == QStringLiteral("permanent_failed")) {
        return core::SyncOutboxStatus::PermanentFailed;
    }
    return core::SyncOutboxStatus::Pending;
}

QString statusToString(core::SyncOutboxStatus status)
{
    switch (status) {
    case core::SyncOutboxStatus::Applied:
        return QStringLiteral("applied");
    case core::SyncOutboxStatus::PermanentFailed:
        return QStringLiteral("permanent_failed");
    case core::SyncOutboxStatus::Pending:
        return QStringLiteral("pending");
    }
    return QStringLiteral("pending");
}

core::SyncOutboxEntry entryFromQuery(const QSqlQuery& query)
{
    const QByteArray opJson = query.value(1).toByteArray();
    core::SyncOutboxEntry entry;
    entry.id = query.value(0).toInt();
    if (const auto restored = core::SyncOpCodec::deserialize(QJsonDocument::fromJson(opJson).object())) {
        entry.op = *restored;
    }
    entry.status = statusFromString(query.value(2).toString());
    entry.attempts = query.value(3).toInt();
    entry.lastError = query.value(4).toString();
    return entry;
}

} // namespace

SyncOutboxRepository::SyncOutboxRepository(Database& db)
    : m_db(db)
{
}

int SyncOutboxRepository::enqueue(const core::SyncOperation& op)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO sync_outbox (op_json, status, attempts, last_error, created_at) "
                       "VALUES (?, 'pending', 0, '', ?)"));
    query.addBindValue(QString::fromUtf8(core::SyncOpCodec::serialize(op)));
    query.addBindValue(nowIso());
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

std::optional<core::SyncOutboxEntry> SyncOutboxRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM sync_outbox WHERE id = ?").arg(QLatin1StringView(kOutboxColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return entryFromQuery(query);
}

std::vector<core::SyncOutboxEntry> SyncOutboxRepository::findPending(int limit) const
{
    std::vector<core::SyncOutboxEntry> entries;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM sync_outbox "
                                 "WHERE status = 'pending' ORDER BY id LIMIT ?")
                      .arg(QLatin1StringView(kOutboxColumns)));
    query.addBindValue(limit);
    if (!query.exec()) {
        return entries;
    }
    while (query.next()) {
        entries.push_back(entryFromQuery(query));
    }
    return entries;
}

int SyncOutboxRepository::countPending() const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM sync_outbox WHERE status = 'pending'"));
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

bool SyncOutboxRepository::markApplied(int id)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE sync_outbox SET status = 'applied' WHERE id = ?"));
    query.addBindValue(id);
    return query.exec();
}

bool SyncOutboxRepository::markPermanentFailed(int id, const QString& error)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "UPDATE sync_outbox SET status = 'permanent_failed', last_error = ? WHERE id = ?"));
    query.addBindValue(error);
    query.addBindValue(id);
    return query.exec();
}

bool SyncOutboxRepository::recordAttempt(int id)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE sync_outbox SET attempts = attempts + 1 WHERE id = ?"));
    query.addBindValue(id);
    return query.exec() && query.numRowsAffected() == 1;
}

} // namespace app::data