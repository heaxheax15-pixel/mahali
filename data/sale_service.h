#pragma once

#include <QString>
#include <QVector>

#include "applied_op_repository.h"
#include "database.h"
#include "customer_transaction_item_repository.h"
#include "customer_transaction_repository.h"
#include "sale_item_repository.h"
#include "cash_movement_repository.h"
#include "cash_session_repository.h"
#include "product_repository.h"
#include "sale_repository.h"
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

class SaleService {
public:
    explicit SaleService(Database& db);

    SaleRecordResult recordSale(const QVector<core::SaleItem>& items, int cashSessionId, const QString& deviceId,
                                bool allowOversold, const core::SyncApplyToken* applyToken = nullptr);

    SaleRecordResult recordCustomerDebt(int customerId, const QVector<core::SaleItem>& items,
                                        const QString& deviceId, bool allowOversold,
                                        const core::SyncApplyToken* applyToken = nullptr);

    int reverseSale(int saleId, int cashSessionId);

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
};

} // namespace app::data