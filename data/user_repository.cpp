#include "user_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

UserRepository::UserRepository(Database& db)
    : m_db(db)
{
}

namespace {

const char* kUserColumns = "id, name, role, pin, active";

core::User userFromQuery(const QSqlQuery& query)
{
    core::User user;
    user.id = query.value(0).toInt();
    user.name = query.value(1).toString();
    user.role = query.value(2).toString();
    user.pin = query.value(3).toString();
    user.active = query.value(4).toInt() != 0;
    return user;
}

} // namespace

std::optional<core::User> UserRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM users WHERE id = ?").arg(QLatin1String(kUserColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return userFromQuery(query);
}

std::vector<core::User> UserRepository::findAll() const
{
    std::vector<core::User> users;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM users ORDER BY name").arg(QLatin1String(kUserColumns)));
    if (!query.exec()) {
        return users;
    }
    while (query.next()) {
        users.push_back(userFromQuery(query));
    }
    return users;
}

QVector<core::User> UserRepository::listActive() const
{
    QVector<core::User> users;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM users WHERE active = 1 ORDER BY name")
                      .arg(QLatin1String(kUserColumns)));
    if (!query.exec()) {
        return users;
    }
    while (query.next()) {
        users.push_back(userFromQuery(query));
    }
    return users;
}

std::optional<core::User> UserRepository::findByPin(const QString& pin) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM users WHERE pin = ? AND active = 1 ORDER BY id LIMIT 1")
            .arg(QLatin1String(kUserColumns)));
    query.addBindValue(pin);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return userFromQuery(query);
}

std::optional<core::User> UserRepository::findByName(const QString& name) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM users WHERE name = ? ORDER BY id LIMIT 1")
            .arg(QLatin1String(kUserColumns)));
    query.addBindValue(name);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return userFromQuery(query);
}

bool UserRepository::savePin(int id, const QString& pin)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE users SET pin = ? WHERE id = ?"));
    query.addBindValue(pin);
    query.addBindValue(id);
    return query.exec() && query.numRowsAffected() > 0;
}

bool UserRepository::setActive(int id, bool active)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE users SET active = ? WHERE id = ?"));
    query.addBindValue(active ? 1 : 0);
    query.addBindValue(id);
    return query.exec() && query.numRowsAffected() > 0;
}

bool UserRepository::hasAny() const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT 1 FROM users LIMIT 1"));
    return query.exec() && query.next();
}

QVector<core::User> UserRepository::listAll() const
{
    QVector<core::User> users;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM users ORDER BY name").arg(QLatin1String(kUserColumns)));
    if (!query.exec()) {
        return users;
    }
    while (query.next()) {
        users.push_back(userFromQuery(query));
    }
    return users;
}

bool UserRepository::remove(int id)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("DELETE FROM users WHERE id = ?"));
    query.addBindValue(id);
    return query.exec() && query.numRowsAffected() > 0;
}

int UserRepository::save(const core::User& user)
{
    QSqlQuery query(m_db.handle());
    if (user.id == 0) {
        query.prepare(QStringLiteral("INSERT INTO users (name, role) VALUES (?, ?)"));
        query.addBindValue(user.name);
        query.addBindValue(user.role);
        if (!query.exec()) {
            return 0;
        }
        return query.lastInsertId().toInt();
    }
    query.prepare(QStringLiteral("UPDATE users SET name = ?, role = ? WHERE id = ?"));
    query.addBindValue(user.name);
    query.addBindValue(user.role);
    query.addBindValue(user.id);
    if (!query.exec()) {
        return 0;
    }
    return user.id;
}

} // namespace app::data