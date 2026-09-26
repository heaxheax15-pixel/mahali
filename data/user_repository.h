#pragma once

#include <QString>
#include <QVector>
#include <optional>
#include <vector>

#include "core/user.h"
#include "database.h"

namespace app::data {

class UserRepository {
public:
    explicit UserRepository(Database& db);

    std::optional<core::User> findById(int id) const;
    std::vector<core::User> findAll() const;

    int save(const core::User& user);

    QVector<core::User> listActive() const;
    std::optional<core::User> findByPin(const QString& pin) const;
    std::optional<core::User> findByName(const QString& name) const;
    bool savePin(int id, const QString& pin);
    bool setActive(int id, bool active);
    bool hasAny() const;

private:
    Database& m_db;
};

} // namespace app::data