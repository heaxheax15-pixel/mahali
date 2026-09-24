#pragma once

#include <optional>
#include <vector>

#include "core/sale_item.h"
#include "database.h"

namespace app::data {

class SaleItemRepository {
public:
    explicit SaleItemRepository(Database& db);

    std::vector<core::SaleItem> findBySaleId(int saleId) const;

    int insert(const core::SaleItem& item);

private:
    Database& m_db;
};

} // namespace app::data