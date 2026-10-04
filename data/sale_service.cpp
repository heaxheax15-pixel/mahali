#include "sale_service.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlQuery>

#include "core/cash_movement.h"
#include "cash_drawer.h"
#include "date_utils.h"
#include "audit_log_repository.h"
#include "sale_rules.h"

namespace app::data {

namespace {

// The audit entry for one sale-shaped operation. A free function because it needs
// nothing but the database, and writing it the same way in all four places is the
// point: an operation that forgot to log would be invisible rather than merely
// wrong.
bool writeSaleAudit(Database& db, const QString& action, const QString& target, const QString& deviceId = {})
{
    AuditLogRepository audit(db);
    const int id = deviceId.isEmpty() ? audit.record(action, target)
                                      : audit.recordAs(deviceId, action, target);
    if (id != 0) {
        return true;
    }
    qWarning() << "audit log write failed for" << action << target;
    return false;
}

} // namespace


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
                                                 const core::SyncApplyToken* applyToken,
                                                 long long adjustmentCents)
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

    // The adjustment joins the total here and nowhere else: the amount owed is
    // lines + adjustment, while the cost of the goods stays at the line prices,
    // which is what the profit and loss statement is built from.
    if (!detail::sumFits(*totalOpt, adjustmentCents)) {
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

    const long long total = *totalOpt + adjustmentCents;
    const long long cogs = *cogsOpt;

    core::CustomerTransaction transaction;
    transaction.customerId = customerId;
    transaction.amountCents = total;
    transaction.adjustmentCents = adjustmentCents;
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

    if (!writeSaleAudit(m_db, QStringLiteral("customer_debt"),
                        QStringLiteral("transaction %1, customer %2")
                            .arg(transactionId)
                            .arg(customerId),
                        applyToken ? applyToken->deviceId : QString())) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the credit sale could not be recorded in the audit log")
            : m_db.lastError();
        return result;
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
                                         const core::SyncApplyToken* applyToken,
                                         long long adjustmentCents)
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

    // The adjustment is added to what the drawer is told about and to nothing
    // else. The cost below is left at the line prices on purpose: a discount given
    // on the invoice does not make the goods cheaper to have sold, and folding it
    // into the cost would turn a discount into a margin.
    if (!detail::sumFits(*totalOpt, adjustmentCents)) {
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

    const long long total = *totalOpt + adjustmentCents;
    const long long cogs = *cogsOpt;

    core::Sale sale;
    sale.createdAt = QDateTime::currentDateTime();
    sale.totalCents = total;
    sale.adjustmentCents = adjustmentCents;
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

    QString movementError;
    const std::optional<int> movementId = cashDrawer::recordMoneyIn(
        m_db, session->id, core::cashMovementType::kSale, total,
        QStringLiteral("Sale #%1").arg(saleId), QStringLiteral("the sale"),
        QStringLiteral("sale"), saleId, &movementError);
    if (!movementId.has_value()) {
        m_db.rollback();
        result.error = movementError;
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

    if (!writeSaleAudit(m_db, QStringLiteral("sale"),
                        QStringLiteral("sale %1, session %2").arg(saleId).arg(cashSessionId),
                        applyToken ? applyToken->deviceId : QString())) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the sale could not be recorded in the audit log")
            : m_db.lastError();
        return result;
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
    // Mirrored with its sign flipped, for the same reason the total is: the
    // reversal's own lines below sum to the negative of the original's, and an
    // adjustment left at 0 would make the row claim a total its own items do not
    // add up to.
    reversal.adjustmentCents = -original->adjustmentCents;
    reversal.deviceId = original->deviceId;
    reversal.occasionId = original->occasionId;
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

    QString movementError;
    const std::optional<int> movementId = cashDrawer::recordMoneyOut(
        m_db, session->id, core::cashMovementType::kRefund, original->totalCents,
        QStringLiteral("Refund of Sale #%1").arg(saleId), QStringLiteral("the refund"),
        QStringLiteral("sale"), saleId, &movementError);
    if (!movementId.has_value()) {
        m_db.rollback();
        r.error = movementError;
        return r;
    }

    if (!writeSaleAudit(m_db, QStringLiteral("sale_refund"),
                        QStringLiteral("sale %1, session %2").arg(saleId).arg(cashSessionId))) {
        m_db.rollback();
        r.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the refund could not be recorded in the audit log")
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

SaleReverseResult SaleService::reverseCustomerDebt(int transactionId)
{
    SaleReverseResult r;

    const auto original = m_customerTransactions.findById(transactionId);
    if (!original.has_value()) {
        r.error = m_db.lastError().isEmpty() ? QStringLiteral("the credit sale does not exist")
                                             : m_db.lastError();
        return r;
    }

    // A payment settles debt by writing a negative row here, and one of those is
    // not a credit sale waiting to be cancelled. Only a positive row is a sale.
    // Reversing one would book a second amount onto a ledger that was already
    // credited — the customer's debt would shrink by the payment as well.
    if (original->amountCents <= 0) {
        r.error = QStringLiteral("only a credit sale can be cancelled, and this is not one");
        return r;
    }

    if (!m_db.beginTransaction()) {
        r.error = m_db.lastError().isEmpty()
            ? QStringLiteral("could not start the transaction")
            : m_db.lastError();
        return r;
    }

    // Same reason as in reverseSale, and the same placement: inside the
    // transaction, so the check and the rows it guards commit together. No
    // unique index backs reversed_transaction_id yet, so this lookup is the only
    // thing stopping a second cancellation of the same sale.
    QSqlQuery existing(m_db.handle());
    existing.prepare(
        QStringLiteral("SELECT 1 FROM customer_transactions WHERE reversed_transaction_id = ? LIMIT 1"));
    existing.addBindValue(transactionId);
    if (!existing.exec()) {
        m_db.rollback();
        r.error = m_db.lastError().isEmpty()
            ? QStringLiteral("could not check for an earlier reversal")
            : m_db.lastError();
        return r;
    }
    if (existing.next()) {
        m_db.rollback();
        r.error = QStringLiteral("this credit sale has already been cancelled");
        return r;
    }

    core::CustomerTransaction reversal;
    reversal.customerId = original->customerId;
    reversal.amountCents = -original->amountCents;
    reversal.adjustmentCents = -original->adjustmentCents;
    reversal.createdAt = QDateTime::currentDateTime();
    reversal.reversedTransactionId = transactionId;
    const int reversalId = m_customerTransactions.insert(reversal);
    if (reversalId == 0) {
        m_db.rollback();
        r.error = m_db.lastError().isEmpty() ? QStringLiteral("could not write the cancellation")
                                             : m_db.lastError();
        return r;
    }

    // The goods come back, at the cost they went out at — the reversal carries
    // its own items so the negative quantity and the negative total can never
    // drift apart.
    const auto originalItems = m_customerTransactionItems.findByTransactionId(transactionId);
    for (const core::CustomerTransactionItem& originalItem : originalItems) {
        core::CustomerTransactionItem reversalItem = originalItem;
        reversalItem.id = 0;
        reversalItem.customerTransactionId = reversalId;
        reversalItem.quantity = -originalItem.quantity;
        reversalItem.reversedId = originalItem.id;
        if (m_customerTransactionItems.insert(reversalItem) == 0) {
            m_db.rollback();
            r.error = m_db.lastError().isEmpty()
                ? QStringLiteral("could not write the cancelled sale item")
                : m_db.lastError();
            return r;
        }

        core::StockMovement movement;
        movement.productId = originalItem.productId;
        movement.delta = originalItem.quantity;
        movement.reason = QStringLiteral("customer_debt_reversal");
        movement.createdAt = QDateTime::currentDateTime();
        if (m_stockMovements.insert(movement) == 0) {
            m_db.rollback();
            r.error = m_db.lastError().isEmpty()
                ? QStringLiteral("could not write the stock movement")
                : m_db.lastError();
            return r;
        }
    }

    // No cash movement, and the omission is the point. The sale was on account:
    // no money entered the drawer when it was taken, so there is nothing to take
    // out now. Writing a refund here would show the till short by the whole sale
    // for goods that were never in it.

    if (!writeSaleAudit(m_db, QStringLiteral("customer_debt_cancellation"),
                        QStringLiteral("transaction %1, customer %2")
                            .arg(transactionId)
                            .arg(original->customerId))) {
        m_db.rollback();
        r.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the cancellation could not be recorded in the audit log")
            : m_db.lastError();
        return r;
    }

    if (!m_db.commit()) {
        r.error = m_db.lastError().isEmpty() ? QStringLiteral("could not commit the transaction")
                                             : m_db.lastError();
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace app::data