#include "sale_service.h"

#include <QDateTime>

#include "date_utils.h"
#include "sale_rules.h"

namespace app::data {

SaleService::SaleService(Database& db)
    : m_db(db)
    , m_products(db)
    , m_sales(db)
    , m_saleItems(db)
    , m_customerTransactions(db)
    , m_customerTransactionItems(db)
    , m_stockMovements(db)
    , m_cashSessions(db)
    , m_cashMovements(db)
    , m_appliedOps(db)
{
}

SaleRecordResult SaleService::alreadyAppliedResult(const core::AppliedOpRecord& record)
{
    SaleRecordResult result;
    result.ok = true;
    result.alreadyApplied = true;
    result.saleId = record.entityId;
    result.totalCents = record.totalCents;
    result.cogsCents = record.cogsCents;
    return result;
}

bool SaleService::insertAppliedOp(const core::SyncApplyToken& token, core::SyncOpType opType, int entityId,
                                  long long totalCents, long long cogsCents, QString* error)
{
    core::AppliedOpRecord record;
    record.opId = token.opId;
    record.deviceId = token.deviceId;
    record.opType = static_cast<int>(opType);
    record.entityId = entityId;
    record.totalCents = totalCents;
    record.cogsCents = cogsCents;
    record.appliedAt = QDateTime::currentDateTime();
    if (m_appliedOps.insert(record) == 0) {
        if (error) {
            *error = m_db.lastError();
        }
        return false;
    }
    return true;
}

SaleRecordResult SaleService::recordCustomerDebt(int customerId, const QVector<core::SaleItem>& items,
                                                 const QString& deviceId, bool allowOversold,
                                                 const core::SyncApplyToken* applyToken)
{
    SaleRecordResult result;
    if (items.isEmpty()) {
        result.error = QStringLiteral("sale items are empty");
        return result;
    }

    if (applyToken) {
        if (const auto existing = m_appliedOps.findByDeviceOp(applyToken->deviceId, applyToken->opId)) {
            return alreadyAppliedResult(*existing);
        }
    }

QString error;
    const QVector<core::SaleItem> resolved = resolveSaleItems(m_products, items, allowOversold, &error);
    if (resolved.isEmpty()) {
        result.error = error;
        return result;
    }

    if (!m_db.beginTransaction()) {
        result.error = m_db.lastError();
        return result;
    }

    long long total = totalCentsFor(resolved);

    core::CustomerTransaction transaction;
    transaction.customerId = customerId;
    transaction.amountCents = total;
    transaction.createdAt = QDateTime::currentDateTime();
    const int transactionId = m_customerTransactions.insert(transaction);
    if (transactionId == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
    }

    for (const core::SaleItem& item : resolved) {
        core::CustomerTransactionItem persisted;
        persisted.customerTransactionId = transactionId;
        persisted.productId = item.productId;
        persisted.quantity = item.quantity;
        persisted.unitPriceCents = item.unitPriceCents;
        persisted.unitCostCents = item.unitCostCents;
        if (m_customerTransactionItems.insert(persisted) == 0) {
            m_db.rollback();
            result.error = m_db.lastError();
            return result;
        }
    }

    if (!insertSaleStockMovements(m_stockMovements, resolved)) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
    }

    if (applyToken) {
        if (!insertAppliedOp(*applyToken, core::SyncOpType::CustomerDebt, transactionId, total,
                             cogsCentsFor(resolved), &result.error)) {
            m_db.rollback();
            const auto existing = m_appliedOps.findByDeviceOp(applyToken->deviceId, applyToken->opId);
            if (existing.has_value()) {
                return alreadyAppliedResult(*existing);
            }
            return result;
        }
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.saleId = transactionId;
    result.totalCents = total;
    result.cogsCents = cogsCentsFor(resolved);
    return result;
}

SaleRecordResult SaleService::recordSale(const QVector<core::SaleItem>& items, int cashSessionId,
                                         const QString& deviceId, bool allowOversold,
                                         const core::SyncApplyToken* applyToken)
{
    SaleRecordResult result;
    if (items.isEmpty()) {
        result.error = QStringLiteral("sale items are empty");
        return result;
    }

    if (applyToken) {
        if (const auto existing = m_appliedOps.findByDeviceOp(applyToken->deviceId, applyToken->opId)) {
            return alreadyAppliedResult(*existing);
        }
    }

    if (!m_db.beginTransaction()) {
        result.error = m_db.lastError();
        return result;
    }

    const auto session = m_cashSessions.findById(cashSessionId);
    if (!session.has_value() || session->status != QStringLiteral("open")) {
        m_db.rollback();
        result.error = QStringLiteral("cash session is not open");
        return result;
    }

    QString error;
    const QVector<core::SaleItem> resolved = resolveSaleItems(m_products, items, allowOversold, &error);
    if (resolved.isEmpty()) {
        m_db.rollback();
        result.error = error;
        return result;
    }

    long long total = totalCentsFor(resolved);

    core::Sale sale;
    sale.createdAt = QDateTime::currentDateTime();
    sale.totalCents = total;
    sale.deviceId = deviceId;
    sale.oversold = allowOversold;
    const int saleId = m_sales.insert(sale);
    if (saleId == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
    }

    for (const core::SaleItem& item : resolved) {
        core::SaleItem persisted = item;
        persisted.saleId = saleId;
        if (m_saleItems.insert(persisted) == 0) {
            m_db.rollback();
            result.error = m_db.lastError();
            return result;
        }
    }

    if (!insertSaleStockMovements(m_stockMovements, resolved)) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
    }

    core::CashMovement movement;
    movement.sessionId = session->id;
    movement.type = QStringLiteral("sale");
    movement.amountCents = total;
    movement.createdAt = QDateTime::currentDateTime();
    if (m_cashMovements.insert(movement) == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
    }

    if (applyToken) {
        if (!insertAppliedOp(*applyToken, core::SyncOpType::Sale, saleId, total, cogsCentsFor(resolved),
                             &result.error)) {
            m_db.rollback();
            const auto existing = m_appliedOps.findByDeviceOp(applyToken->deviceId, applyToken->opId);
            if (existing.has_value()) {
                return alreadyAppliedResult(*existing);
            }
            return result;
        }
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.saleId = saleId;
    result.totalCents = total;
    result.cogsCents = cogsCentsFor(resolved);
    return result;
}

int SaleService::reverseSale(int saleId, int cashSessionId)
{
    const auto original = m_sales.findById(saleId);
    if (!original.has_value()) {
        return 0;
    }

    if (!m_db.beginTransaction()) {
        return 0;
    }

    const auto session = m_cashSessions.findById(cashSessionId);
    if (!session.has_value() || session->status != QStringLiteral("open")) {
        m_db.rollback();
        return 0;
    }

    core::Sale reversal;
    reversal.createdAt = QDateTime::currentDateTime();
    reversal.totalCents = -original->totalCents;
    reversal.deviceId = original->deviceId;
    reversal.reversedSaleId = saleId;
    const int reversalId = m_sales.insert(reversal);
    if (reversalId == 0) {
        m_db.rollback();
        return 0;
    }

    const auto originalItems = m_saleItems.findBySaleId(saleId);
    for (const core::SaleItem& originalItem : originalItems) {
        core::SaleItem reversalItem = originalItem;
        reversalItem.id = 0;
        reversalItem.saleId = reversalId;
        reversalItem.quantity = -originalItem.quantity;
        reversalItem.reversedId = originalItem.id;
        m_saleItems.insert(reversalItem);

        core::StockMovement movement;
        movement.productId = originalItem.productId;
        movement.delta = originalItem.quantity;
        movement.reason = QStringLiteral("sale_reversal");
        movement.createdAt = QDateTime::currentDateTime();
        m_stockMovements.insert(movement);
    }

    core::CashMovement cashReversal;
    cashReversal.sessionId = session->id;
    cashReversal.type = QStringLiteral("refund");
    cashReversal.amountCents = -original->totalCents;
    cashReversal.createdAt = QDateTime::currentDateTime();
    m_cashMovements.insert(cashReversal);

    if (!m_db.commit()) {
        return 0;
    }
    return reversalId;
}

} // namespace app::data