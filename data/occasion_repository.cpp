#include "occasion_repository.h"

#include <QSqlQuery>

namespace app::data {

OccasionRepository::OccasionRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::Occasion occasionFromQuery(const QSqlQuery& query)
{
    core::Occasion o;
    o.id = query.value(0).toInt();
    o.name = query.value(1).toString();
    o.startsAt = query.value(2).toString();
    o.endsAt = query.value(3).toString();
    o.icon = query.value(4).toString();
    o.active = query.value(5).toInt() != 0;
    o.createdAt = query.value(6).toString();
    return o;
}

const char* kOccasionColumns = "id, name, starts_at, ends_at, icon, active, created_at";

} // namespace

std::optional<core::Occasion> OccasionRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM occasions WHERE id = ?").arg(QLatin1StringView(kOccasionColumns)));
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("OccasionRepository::findById"));
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return occasionFromQuery(query);
}

QVector<core::Occasion> OccasionRepository::findAll() const
{
    QVector<core::Occasion> occasions;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM occasions ORDER BY starts_at DESC").arg(QLatin1StringView(kOccasionColumns)));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("OccasionRepository::findAll"));
        return occasions;
    }
    while (query.next()) {
        occasions.push_back(occasionFromQuery(query));
    }
    return occasions;
}

QVector<core::Occasion> OccasionRepository::findActive() const
{
    QVector<core::Occasion> occasions;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM occasions WHERE active = 1 ORDER BY starts_at DESC")
                      .arg(QLatin1StringView(kOccasionColumns)));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("OccasionRepository::findActive"));
        return occasions;
    }
    while (query.next()) {
        occasions.push_back(occasionFromQuery(query));
    }
    return occasions;
}

int OccasionRepository::insert(const core::Occasion& o)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO occasions (name, starts_at, ends_at, icon, active, created_at) "
                       "VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(o.name);
    // Every text column here is NOT NULL, and a null QString is bound as SQL
    // NULL, which the driver refuses. Callers that never set them hold exactly
    // such a null, so the blank the columns default to is filled in here.
    query.addBindValue(o.startsAt.isNull() ? QStringLiteral("") : o.startsAt);
    query.addBindValue(o.endsAt.isNull() ? QStringLiteral("") : o.endsAt);
    query.addBindValue(o.icon.isNull() ? QStringLiteral("") : o.icon);
    query.addBindValue(o.active ? 1 : 0);
    query.addBindValue(o.createdAt.isNull() ? QStringLiteral("") : o.createdAt);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("OccasionRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool OccasionRepository::update(const core::Occasion& o)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("UPDATE occasions SET name = ?, starts_at = ?, ends_at = ?, icon = ?, active = ? "
                       "WHERE id = ?"));
    query.addBindValue(o.name);
    query.addBindValue(o.startsAt.isNull() ? QStringLiteral("") : o.startsAt);
    query.addBindValue(o.endsAt.isNull() ? QStringLiteral("") : o.endsAt);
    query.addBindValue(o.icon.isNull() ? QStringLiteral("") : o.icon);
    query.addBindValue(o.active ? 1 : 0);
    query.addBindValue(o.id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("OccasionRepository::update"));
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool OccasionRepository::remove(int id)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("DELETE FROM occasions WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("OccasionRepository::remove"));
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool OccasionRepository::setActive(int id, bool active)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE occasions SET active = ? WHERE id = ?"));
    query.addBindValue(active ? 1 : 0);
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("OccasionRepository::setActive"));
        return false;
    }
    return query.numRowsAffected() > 0;
}

} // namespace app::data
