#pragma once

#include <QString>
#include <QVector>

#include "applied_op_repository.h"
#include "database.h"
#include "customer_transaction_item_repository.h"
#include "customer_transaction_repository.h"
#include "occasion_repository.h"
#include "occasion_service.h"
#include "sale_item_repository.h"
#include "cash_movement_repository.h"
#include "cash_session_repository.h"
#include "product_repository.h"
#include "sale_repository.h"
#include "setting_repository.h"
#include "stock_movement_repository.h"

namespace app::data {

struct SaleRecordResult {
    bool ok = false;
    bool alreadyApplied = false;
    int saleId = 0;
    long long totalCents = 0;
    long long cogsCents = 0;
    QString error;
};

// Sale reversal carries no totals of its own to hand back, so this is just the
// outcome plus the reason. It exists because a plain int could only say "no",
// leaving refunds_page with nothing to tell the operator but a guess.
struct SaleReverseResult {
    bool ok = false;
    QString error;
};

class SaleService {
public:
    explicit SaleService(Database& db);

    SaleRecordResult recordSale(const QVector<core::SaleItem>& items, int cashSessionId, const QString& deviceId,
                                bool allowOversold, const core::SyncApplyToken* applyToken = nullptr);

    SaleRecordResult recordCustomerDebt(int customerId, const QVector<core::SaleItem>& items,
                                        const QString& deviceId, bool allowOversold,
                                        const core::SyncApplyToken* applyToken = nullptr);

    SaleReverseResult reverseSale(int saleId, int cashSessionId);

    // Cancels a credit sale. Same shape as reverseSale and for the same reason:
    // nothing is deleted or edited, a negative row is written against the
    // original and the goods go back on the shelf.
    //
    // Deliberately unlike reverseSale, there is no cashSessionId parameter. A
    // credit sale never put money in the drawer, so there is nothing to refund
    // and no till to count — taking a session here would invite a caller to name
    // one and then write a refund into a drawer that never gained the money in
    // the first place.
    SaleReverseResult reverseCustomerDebt(int transactionId);

private:
    bool insertAppliedOp(const core::SyncApplyToken& token, core::SyncOpType opType, int entityId,
                         long long totalCents, long long cogsCents, QString* error);
    SaleRecordResult alreadyAppliedResult(const core::AppliedOpRecord& record);

    Database& m_db;
    ProductRepository m_products;
    SaleRepository m_sales;
    SaleItemRepository m_saleItems;
    CustomerTransactionRepository m_customerTransactions;
    CustomerTransactionItemRepository m_customerTransactionItems;
    StockMovementRepository m_stockMovements;
    CashSessionRepository m_cashSessions;
    CashMovementRepository m_cashMovements;
    AppliedOpRepository m_appliedOps;
    // Built here off the same database rather than injected. Occasions are
    // read-only context for a sale, so there is nothing to configure and no
    // call site to update, and SaleService already owns its other repositories.
    OccasionRepository m_occasions;
    SettingRepository m_settings;
    OccasionService m_occasionService;
};

} // namespace app::data