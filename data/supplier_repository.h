#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "core/supplier.h"
#include "database.h"

namespace app::data {

class SupplierRepository {
public:
    explicit SupplierRepository(Database& db);

    std::optional<core::Supplier> findById(int id) const;
    std::optional<core::Supplier> findByName(const QString& name) const;
    std::vector<core::Supplier> findAll() const;

    int save(const core::Supplier& supplier);

private:
    Database& m_db;
};

} // namespace app::data