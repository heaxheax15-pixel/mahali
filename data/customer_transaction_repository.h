#pragma once

#include <QDateTime>
#include <optional>
#include <vector>

#include "core/customer_transaction.h"
#include "database.h"

namespace app::data {

class CustomerTransactionRepository {
public:
    explicit CustomerTransactionRepository(Database& db);

    std::optional<core::CustomerTransaction> findById(int id) const;
    std::vector<core::CustomerTransaction> findByCustomerId(int customerId) const;
    std::vector<core::CustomerTransaction> findBetween(const QDateTime& from, const QDateTime& to) const;

    int insert(const core::CustomerTransaction& transaction);

private:
    Database& m_db;
};

} // namespace app::data