#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "core/customer.h"
#include "database.h"

namespace app::data {

class CustomerRepository {
public:
    explicit CustomerRepository(Database& db);

    std::optional<core::Customer> findById(int id) const;
    std::vector<core::Customer> findByName(const QString& name) const;
    std::vector<core::Customer> findAll() const;

    int save(const core::Customer& customer);

private:
    Database& m_db;
};

} // namespace app::data