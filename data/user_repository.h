#pragma once

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

private:
    Database& m_db;
};

} // namespace app::data