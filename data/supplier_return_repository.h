#pragma once

#include <optional>
#include <vector>

#include "core/supplier_return.h"
#include "database.h"

namespace app::data {

class SupplierReturnRepository {
public:
    explicit SupplierReturnRepository(Database& db);

    std::optional<core::SupplierReturn> findById(int id) const;
    std::vector<core::SupplierReturn> findBySupplierId(int supplierId) const;
    std::vector<core::SupplierReturn> findByPurchaseId(int purchaseId) const;
    int insert(const core::SupplierReturn& r);
    bool remove(int id);

private:
    Database& m_db;
};

} // namespace app::data
