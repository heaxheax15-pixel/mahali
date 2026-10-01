#pragma once

#include <map>
#include <optional>
#include <vector>

#include "core/sale_item.h"
#include "database.h"

namespace app::data {

class SaleItemRepository {
public:
    explicit SaleItemRepository(Database& db);

    std::vector<core::SaleItem> findBySaleId(int saleId) const;

    // Total units per sale, in one query, for a list of sales on screen at once.
    // A day's till runs to hundreds of sales and the strip that shows them asks
    // for an item count on each row, so calling findBySaleId() per row would be
    // hundreds of round trips every time the page is opened. Sales with no items
    // (a reversal row) are absent from the map rather than mapped to zero, so the
    // caller can tell "no items recorded" from "no items left".
    std::map<int, long long> findQuantitiesBySaleIds(const std::vector<int>& saleIds) const;

    int insert(const core::SaleItem& item);

private:
    Database& m_db;
};

} // namespace app::data