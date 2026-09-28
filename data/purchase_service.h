#pragma once

#include <QString>
#include <QVector>

#include "core/purchase.h"
#include "core/purchase_item.h"
#include "database.h"
#include "product_repository.h"
#include "purchase_item_repository.h"
#include "purchase_repository.h"
#include "stock_movement_repository.h"
#include "supplier_repository.h"
#include "supplier_transaction_repository.h"

namespace app::data {

struct PurchaseResult {
    bool ok = false;
    int purchaseId = 0;
    QString error;
};

class PurchaseService {
public:
    PurchaseService(Database& db, PurchaseRepository& purchases, PurchaseItemRepository& items,
                    ProductRepository& products, StockMovementRepository& stockMovements,
                    SupplierRepository& suppliers, SupplierTransactionRepository& supplierTxs);

    // Records a supplier invoice whole or not at all: the header, its lines, the
    // stock movements, the average costs and the supplier debt all land or none
    // of them do, because a half-written purchase leaves stock that no invoice
    // accounts for.
    PurchaseResult recordPurchase(const core::Purchase& purchase, const QVector<core::PurchaseItem>& items);

private:
    Database& m_db;
    PurchaseRepository& m_purchases;
    PurchaseItemRepository& m_items;
    ProductRepository& m_products;
    StockMovementRepository& m_stockMovements;
    SupplierRepository& m_suppliers;
    SupplierTransactionRepository& m_supplierTxs;
};

} // namespace app::data
