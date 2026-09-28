#include "sale_service.h"

#include <QDateTime>
#include <QSqlQuery>

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
    , m_occasions(db)
    , m_settings(db)
    , m_occasionService(db, m_occasions, m_settings)
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

    const std::optional<long long> totalOpt = totalCentsFor(resolved);
    if (!totalOpt.has_value()) {
        result.error = QStringLiteral("the sale total is too large to record");
        return result;
    }

    const std::optional<long long> cogsOpt = cogsCentsFor(resolved);
    if (!cogsOpt.has_value()) {
        result.error = QStringLiteral("the sale cost is too large to record");
        return result;
    }

    if (!m_db.beginTransaction()) {
        result.error = m_db.lastError();
        return result;
    }

    const long long total = *totalOpt;
    const long long cogs = *cogsOpt;

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
        if (!insertAppliedOp(*applyToken, core::SyncOpType::CustomerDebt, transactionId, total, cogs,
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
    result.saleId = transactionId;
    result.totalCents = total;
    result.cogsCents = cogs;
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

    const std::optional<long long> totalOpt = totalCentsFor(resolved);
    if (!totalOpt.has_value()) {
        m_db.rollback();
        result.error = QStringLiteral("the sale total is too large to record");
        return result;
    }

    const std::optional<long long> cogsOpt = cogsCentsFor(resolved);
    if (!cogsOpt.has_value()) {
        m_db.rollback();
        result.error = QStringLiteral("the sale cost is too large to record");
        return result;
    }

    const long long total = *totalOpt;
    const long long cogs = *cogsOpt;

    core::Sale sale;
    sale.createdAt = QDateTime::currentDateTime();
    sale.totalCents = total;
    sale.deviceId = deviceId;
    sale.oversold = allowOversold;
    // Stamped with whatever occasion is running at the moment of the sale, so the
    // reports can split a day's takings by event. Read after the transaction
    // opens: the setting is a single row, and reading it here keeps the sale and
    // the occasion it was attributed to inside one snapshot.
    if (const std::optional<core::Occasion> occasion = m_occasionService.current()) {
        sale.occasionId = occasion->id;
    }
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
        if (!insertAppliedOp(*applyToken, core::SyncOpType::Sale, saleId, total, cogs, &result.error)) {
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
    result.cogsCents = cogs;
    return result;
}

SaleReverseResult SaleService::reverseSale(int saleId, int cashSessionId)
{
    SaleReverseResult r;

    const auto original = m_sales.findById(saleId);
    if (!original.has_value()) {
        r.error = m_db.lastError().isEmpty() ? QStringLiteral("reverseSale: the sale does not exist")
                                             : m_db.lastError();
        return r;
    }

    if (!m_db.beginTransaction()) {
        r.error = m_db.lastError().isEmpty()
            ? QStringLiteral("reverseSale: could not start the transaction")
            : m_db.lastError();
        return r;
    }

    // Nothing stops the same sale being reversed twice, and reversed_sale_id
    // has no unique index, so the guard has to be a lookup. Inside the
    // transaction on purpose: the lookup and the inserts it protects commit or
    // roll back as one, so a second click that arrives while the first is still
    // running cannot pass the check against a snapshot that is about to change.
    QSqlQuery existing(m_db.handle());
    existing.prepare(QStringLiteral("SELECT 1 FROM sales WHERE reversed_sale_id = ? LIMIT 1"));
    existing.addBindValue(saleId);
    if (!existing.exec()) {
        m_db.rollback();
        r.error = m_db.lastError().isEmpty()
            ? QStringLiteral("reverseSale: could not check for an earlier reversal")
            : m_db.lastError();
        return r;
    }
    if (existing.next()) {
        m_db.rollback();
        r.error = QStringLiteral("reverseSale: already reversed");
        return r;
    }

    const auto session = m_cashSessions.findById(cashSessionId);
    if (!session.has_value() || session->status != QStringLiteral("open")) {
        m_db.rollback();
        r.error = m_db.lastError().isEmpty() ? QStringLiteral("reverseSale: the cash session is not open")
                                             : m_db.lastError();
        return r;
    }

    core::Sale reversal;
    reversal.createdAt = QDateTime::currentDateTime();
    reversal.totalCents = -original->totalCents;
    reversal.deviceId = original->deviceId;
    reversal.reversedSaleId = saleId;
    const int reversalId = m_sales.insert(reversal);
    if (reversalId == 0) {
        m_db.rollback();
        r.error = m_db.lastError().isEmpty()
            ? QStringLiteral("reverseSale: could not write the reversed sale")
            : m_db.lastError();
        return r;
    }

    const auto originalItems = m_saleItems.findBySaleId(saleId);
    for (const core::SaleItem& originalItem : originalItems) {
        core::SaleItem reversalItem = originalItem;
        reversalItem.id = 0;
        reversalItem.saleId = reversalId;
        reversalItem.quantity = -originalItem.quantity;
        reversalItem.reversedId = originalItem.id;
        if (m_saleItems.insert(reversalItem) == 0) {
            m_db.rollback();
            r.error = m_db.lastError().isEmpty()
                ? QStringLiteral("reverseSale: could not write the reversed sale item")
                : m_db.lastError();
            return r;
        }

        core::StockMovement movement;
        movement.productId = originalItem.productId;
        movement.delta = originalItem.quantity;
        movement.reason = QStringLiteral("sale_reversal");
        movement.createdAt = QDateTime::currentDateTime();
        if (m_stockMovements.insert(movement) == 0) {
            m_db.rollback();
            r.error = m_db.lastError().isEmpty()
                ? QStringLiteral("reverseSale: could not write the stock movement")
                : m_db.lastError();
            return r;
        }
    }

    core::CashMovement cashReversal;
    cashReversal.sessionId = session->id;
    cashReversal.type = QStringLiteral("refund");
    cashReversal.amountCents = -original->totalCents;
    cashReversal.createdAt = QDateTime::currentDateTime();
    if (m_cashMovements.insert(cashReversal) == 0) {
        m_db.rollback();
        r.error = m_db.lastError().isEmpty()
            ? QStringLiteral("reverseSale: could not write the refund cash movement")
            : m_db.lastError();
        return r;
    }

    if (!m_db.commit()) {
        r.error = m_db.lastError().isEmpty()
            ? QStringLiteral("reverseSale: could not commit the transaction")
            : m_db.lastError();
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace app::data