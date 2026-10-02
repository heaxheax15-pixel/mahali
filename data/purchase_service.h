#pragma once

#include <optional>
#include <QString>
#include <QVector>

#include "core/purchase.h"
#include "core/purchase_item.h"
#include "core/supplier_payment.h"
#include "cash_drawer.h"
#include "database.h"
#include "occasion_repository.h"
#include "occasion_service.h"
#include "product_repository.h"
#include "purchase_item_repository.h"
#include "purchase_repository.h"
#include "setting_repository.h"
#include "stock_movement_repository.h"
#include "supplier_repository.h"
#include "supplier_payment_repository.h"

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
                    SupplierRepository& suppliers, SupplierPaymentRepository& supplierPayments);

    // Records a supplier invoice whole or not at all: the header, its lines, the
    // stock movements, the average costs and the supplier debt all land or none
    // of them do, because a half-written purchase leaves stock that no invoice
    // accounts for.
    //
    // paidCents on the invoice is what was handed over with it. When it is above
    // zero and method is Cash, cashSessionId must name an open session and a
    // negative cash_movements row is written for the amount inside the same
    // transaction: an invoice paid out of the drawer is the drawer being short by
    // that much, and the session has to be able to say so. When it is zero there
    // is no money to move, so no session is needed and none is looked for. A
    // Credit or Bank payment settles the supplier's balance without the drawer
    // changing, and needs no session either.
    //
    // The session is passed in rather than found, and validated inside the
    // transaction, so the movement is booked against the till the operator meant
    // and a till closed while they were typing is refused instead of written to.
PurchaseResult recordPurchase(const core::Purchase& purchase,
                                  const QVector<core::PurchaseItem>& items,
                                  core::SupplierPaymentMethod method = core::SupplierPaymentMethod::Cash,
                                  std::optional<int> cashSessionId = std::nullopt);

    // Cancels a purchase entirely. The same append-only discipline: a negative
    // header is written with reversed_id against the original, negative items
    // with reversed_id against each original item, and stock comes back through
    // positive stock movements. If the original had a cash payment, the reversal
    // writes a positive cash_movement (the money returns to the drawer) and a
    // supplier_payment reversal row — the whole thing is one transaction so
    // nothing can come apart.
    //
    // Does not recompute average costs (PMP). A void purchase returns goods at
    // the cost they went out at; the PMP is a running average and changing it
    // retroactively would make the ledger disagree with the stock on the shelf.
    PurchaseResult voidPurchase(int purchaseId, std::optional<int> cashSessionId);

private:
    Database& m_db;
    PurchaseRepository& m_purchases;
    PurchaseItemRepository& m_items;
    ProductRepository& m_products;
    StockMovementRepository& m_stockMovements;
    SupplierRepository& m_suppliers;
    SupplierPaymentRepository& m_supplierPayments;
    // Built here off the same database rather than injected, for the same reason
    // as in SaleService: occasions are read-only context for a purchase, and
    // every call site of this constructor would otherwise have to supply them.
    OccasionRepository m_occasions;
    SettingRepository m_settings;
    OccasionService m_occasionService;
};

} // namespace app::data
