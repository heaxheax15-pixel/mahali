#pragma once

#include <vector>

#include "core/purchase_item.h"
#include "database.h"

namespace app::data {

class PurchaseItemRepository {
public:
    explicit PurchaseItemRepository(Database& db);

    QVector<core::PurchaseItem> findByPurchase(int purchaseId) const;

    // Inserts a single item. Returns the inserted id, or 0 on failure.
    int insert(const core::PurchaseItem& item);

    bool removeByPurchase(int purchaseId);

private:
    Database& m_db;
};

} // namespace app::data