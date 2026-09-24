#pragma once

#include <optional>
#include <vector>

#include "core/customer_transaction_item.h"
#include "database.h"

namespace app::data {

class CustomerTransactionItemRepository {
public:
    explicit CustomerTransactionItemRepository(Database& db);

    std::vector<core::CustomerTransactionItem> findByTransactionId(int transactionId) const;

    int insert(const core::CustomerTransactionItem& item);

private:
    Database& m_db;
};

} // namespace app::data