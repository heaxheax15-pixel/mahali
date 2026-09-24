#include "user_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

UserRepository::UserRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::User userFromQuery(const QSqlQuery& query)
{
    core::User user;
    user.id = query.value(0).toInt();
    user.name = query.value(1).toString();
    user.role = query.value(2).toString();
    return user;
}

} // namespace

std::optional<core::User> UserRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, name, role FROM users WHERE id = ?"));
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
    query.prepare(QStringLiteral("SELECT id, name, role FROM users ORDER BY name"));
    if (!query.exec()) {
        return users;
    }
    while (query.next()) {
        users.push_back(userFromQuery(query));
    }
    return users;
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