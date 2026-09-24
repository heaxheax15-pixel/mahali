#include "sale_service.h"

#include <QDateTime>

#include "date_utils.h"

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
{
}

long long SaleService::cogsCentsFor(const QVector<core::SaleItem>& items) const
{
    long long cogs = 0;
    for (const core::SaleItem& item : items) {
        cogs += item.unitCostCents * item.quantity;
    }
    return cogs;
}

bool SaleService::insertStockMovements(const QVector<core::SaleItem>& items)
{
    for (const core::SaleItem& item : items) {
        core::StockMovement movement;
        movement.productId = item.productId;
        movement.delta = -item.quantity;
        movement.reason = QStringLiteral("sale");
        movement.createdAt = QDateTime::currentDateTime();
        if (m_stockMovements.insert(movement) == 0) {
            return false;
        }
    }
    return true;
}

QVector<core::SaleItem> SaleService::resolveItems(const QVector<core::SaleItem>& items, bool allowOversold,
                                                  QString* error)
{
    QVector<core::SaleItem> resolved = items;
    for (core::SaleItem& item : resolved) {
        const auto product = m_products.findById(item.productId);
        if (!product.has_value()) {
            if (error) {
                *error = QStringLiteral("unknown product id %1").arg(item.productId);
            }
            return {};
        }
        if (item.unitPriceCents <= 0) {
            item.unitPriceCents = product->salePriceCents;
        }
        item.unitCostCents = product->costPriceCents;
        if (!allowOversold && product->quantity < item.quantity) {
            if (error) {
                *error = QStringLiteral("insufficient stock for product %1").arg(product->name);
            }
            return {};
        }
    }
    return resolved;
}

SaleRecordResult SaleService::recordCustomerDebt(int customerId, const QVector<core::SaleItem>& items,
                                                 const QString& deviceId, bool allowOversold)
{
    SaleRecordResult result;
    if (items.isEmpty()) {
        result.error = QStringLiteral("sale items are empty");
        return result;
    }

    QString error;
    const QVector<core::SaleItem> resolved = resolveItems(items, allowOversold, &error);
    if (resolved.isEmpty()) {
        result.error = error;
        return result;
    }

    if (!m_db.beginTransaction()) {
        result.error = m_db.lastError();
        return result;
    }

    long long total = 0;
    for (const core::SaleItem& item : resolved) {
        total += item.unitPriceCents * item.quantity;
    }

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

    if (!insertStockMovements(resolved)) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
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
                                         const QString& deviceId, bool allowOversold)
{
    SaleRecordResult result;
    if (items.isEmpty()) {
        result.error = QStringLiteral("sale items are empty");
        return result;
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
    const QVector<core::SaleItem> resolved = resolveItems(items, allowOversold, &error);
    if (resolved.isEmpty()) {
        m_db.rollback();
        result.error = error;
        return result;
    }

    long long total = 0;
    for (const core::SaleItem& item : resolved) {
        total += item.unitPriceCents * item.quantity;
    }

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

    if (!insertStockMovements(resolved)) {
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