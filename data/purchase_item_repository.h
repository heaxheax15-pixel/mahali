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

    // Inserts a full list of items inside a single transaction.
    // Returns true if all succeeded, false on first failure with rollback.
    bool insertAll(int purchaseId, const QVector<core::PurchaseItem>& items);

    bool removeByPurchase(int purchaseId);

private:
    Database& m_db;
};

} // namespace app::data