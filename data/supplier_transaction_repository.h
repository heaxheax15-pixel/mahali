#pragma once

#include <QDateTime>
#include <optional>
#include <vector>

#include "core/supplier_transaction.h"
#include "database.h"

namespace app::data {

class SupplierTransactionRepository {
public:
    explicit SupplierTransactionRepository(Database& db);

    std::optional<core::SupplierTransaction> findById(int id) const;
    std::vector<core::SupplierTransaction> findBySupplierId(int supplierId) const;
    std::vector<core::SupplierTransaction> findBetween(const QDateTime& from, const QDateTime& to) const;

    int insert(const core::SupplierTransaction& transaction);

private:
    Database& m_db;
};

} // namespace app::data