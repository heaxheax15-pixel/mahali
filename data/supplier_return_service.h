#pragma once

#include <QString>
#include <QVector>

#include "core/supplier_return_item.h"
#include "database.h"
#include "product_repository.h"
#include "purchase_repository.h"
#include "stock_movement_repository.h"
#include "supplier_return_item_repository.h"
#include "supplier_return_repository.h"
#include "supplier_repository.h"

namespace app::data {

struct SupplierReturnResult {
    bool ok = false;
    int returnId = 0;
    QString error;
    // Set when the return was recorded but left the shelves below zero. That is
    // allowed, so it is reported here rather than refused: the goods did go back
    // to the supplier, and refusing the entry would hide a stock count that is
    // already wrong.
    QString warning;
};

class SupplierReturnService {
public:
    SupplierReturnService(Database& db,
                          SupplierReturnRepository& returns,
                          SupplierReturnItemRepository& returnItems,
                          SupplierRepository& suppliers,
                          PurchaseRepository& purchases,
                          ProductRepository& products,
                          StockMovementRepository& stockMovements);

    // Pattern (a): a return against one invoice, priced line by line. What the
    // lines add up to is what the supplier is credited.
    SupplierReturnResult recordLinkedReturn(int supplierId,
                                            int purchaseId,
                                            const QVector<core::SupplierReturnItem>& items,
                                            const QString& returnedAt,
                                            const QString& note);

    // Pattern (b): a return for its amount alone, with no invoice and no lines
    // behind it, for goods that came back outside any purchase.
    SupplierReturnResult recordGeneralReturn(int supplierId,
                                             long long amountCents,
                                             const QString& returnedAt,
                                             const QString& note);

private:
    Database& m_db;
    SupplierReturnRepository& m_returns;
    SupplierReturnItemRepository& m_returnItems;
    SupplierRepository& m_suppliers;
    PurchaseRepository& m_purchases;
    ProductRepository& m_products;
    StockMovementRepository& m_stockMovements;
};

} // namespace app::data
