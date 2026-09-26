#pragma once

#include <optional>

#include "core/user.h"
#include "database.h"

namespace app::data {

class AdminSecretRepository {
public:
    explicit AdminSecretRepository(Database& db);

    bool setMaster(int userId, const QString& password);
    bool verifyMaster(int userId, const QString& password) const;
    std::optional<int> findAdminByMaster(const QString& password) const;

private:
    Database& m_db;
};

} // namespace app::data