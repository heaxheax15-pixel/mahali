#include "device_ledger_service.h"

#include <QDateTime>

#include "core/cash_movement.h"
#include "audit_log_repository.h"
#include "cash_drawer.h"
#include "device_identity.h"
#include "sale_rules.h"

namespace app::data {

namespace {

bool writeDeviceAudit(Database& db, const QString& deviceId, const QString& action, int entityId)
{
    AuditLogRepository audit(db);
    return audit.recordAs(deviceId, action, QStringLiteral("%1 #%2").arg(action).arg(entityId)) != 0;
}

} // namespace

DeviceLedgerService::DeviceLedgerService(Database& db, const QString& deviceId)
    : m_db(db)
    , m_deviceId(deviceId)
    , m_products(db)
    , m_sales(db)
    , m_saleItems(db)
    , m_customerTransactions(db)
    , m_customerTransactionItems(db)
    , m_stockMovements(db)
    , m_cashSessions(db)
    , m_cashMovements(db)
    , m_payments(db)
    , m_syncSequence(db)
    , m_outbox(db)
    , m_occasions(db)
    , m_settings(db)
    , m_occasionService(db, m_occasions, m_settings)
{
}

DeviceLedgerService::DeviceLedgerService(Database& db)
    : DeviceLedgerService(db, DeviceIdentity::ensure(db))
{
}

bool DeviceLedgerService::rejectWithoutIdentity(DeviceOpResult* result) const
{
    if (isValid()) {
        return false;
    }
    if (result) {
        *result = DeviceOpResult();
        result->error = QStringLiteral("device id is empty: the device identity could not be minted");
    }
    return true;
}

int DeviceLedgerService::mintAndEnqueue(core::SyncOpType type, int entityId, long long totalCents,
                                        const QVector<core::SaleItem>& resolved, const QString& note,
                                        QString* error)
{
    const int opId = m_syncSequence.nextOpId();
    if (opId == 0) {
        if (error) {
            *error = m_db.lastError();
        }
        return 0;
    }

    core::SyncOperation op;
    op.opId = opId;
    op.type = type;
    op.entityId = entityId;
    op.amountCents = totalCents;
    op.occurredAt = QDateTime::currentDateTime();
    op.note = note;
    op.deviceId = m_deviceId;

    op.items.reserve(resolved.size());
    for (const core::SaleItem& item : resolved) {
        core::SyncItem syncItem;
        syncItem.productId = item.productId;
        syncItem.quantity = item.quantity;
        syncItem.unitPriceCents = item.unitPriceCents;
        op.items.append(syncItem);
    }

    const int outboxId = m_outbox.enqueue(op);
    if (outboxId == 0) {
        if (error) {
            *error = m_db.lastError();
        }
        return 0;
    }
    Q_UNUSED(outboxId);
    return opId;
}

DeviceOpResult DeviceLedgerService::recordSale(const QVector<core::SaleItem>& items, int cashSessionId)
{
    DeviceOpResult result;
    if (rejectWithoutIdentity(&result)) {
        return result;
    }
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

    QString resolveError;
    const QVector<core::SaleItem> resolved = resolveSaleItems(m_products, items, /*allowOversold=*/false,
                                                              &resolveError);
    if (resolved.isEmpty()) {
        m_db.rollback();
        result.error = resolveError;
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
    sale.deviceId = m_deviceId;
    sale.oversold = false;
    // Stamped with whatever occasion is running, exactly as the server-side sale
    // service stamps it. A device that skipped this wrote sales that belonged to
    // no event at all, and the occasion report then showed a shortfall that was
    // only an omission here — the takings were in the daily total and missing
    // from the event they belonged to.
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

    QString error;
    const int opId = mintAndEnqueue(core::SyncOpType::Sale, saleId, total, resolved, QStringLiteral(""),
                                    &error);
    if (opId == 0) {
        m_db.rollback();
        result.error = error;
        return result;
    }

    if (!writeDeviceAudit(m_db, m_deviceId, QStringLiteral("sale"), saleId)) {
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
    result.opId = opId;
    result.entityId = saleId;
    result.totalCents = total;
    result.cogsCents = cogs;
    return result;
}

DeviceOpResult DeviceLedgerService::recordCustomerDebt(int customerId, const QVector<core::SaleItem>& items)
{
    DeviceOpResult result;
    if (rejectWithoutIdentity(&result)) {
        return result;
    }
    if (items.isEmpty()) {
        result.error = QStringLiteral("sale items are empty");
        return result;
    }
    if (customerId <= 0) {
        result.error = QStringLiteral("customer id is invalid");
        return result;
    }

    if (!m_db.beginTransaction()) {
        result.error = m_db.lastError();
        return result;
    }

    QString resolveError;
    const QVector<core::SaleItem> resolved = resolveSaleItems(m_products, items, /*allowOversold=*/false,
                                                              &resolveError);
    if (resolved.isEmpty()) {
        m_db.rollback();
        result.error = resolveError;
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
        // Copied through from resolved, like every other field -- dropping these two
        // writes pieces_consumed = 0, and the server would then refuse to reverse a
        // device-booked credit sale (sale_service's reversal guard).
        persisted.unitKind = item.unitKind;
        persisted.piecesConsumed = item.piecesConsumed;
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

    QString error;
    const int opId = mintAndEnqueue(core::SyncOpType::CustomerDebt, customerId, total, resolved,
                                    QStringLiteral(""), &error);
    if (opId == 0) {
        m_db.rollback();
        result.error = error;
        return result;
    }

    if (!writeDeviceAudit(m_db, m_deviceId, QStringLiteral("customer_debt"), transactionId)) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the debt could not be recorded in the audit log")
            : m_db.lastError();
        return result;
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.opId = opId;
    result.entityId = transactionId;
    result.totalCents = total;
    result.cogsCents = cogs;
    return result;
}

DeviceOpResult DeviceLedgerService::recordCustomerPayment(int customerId, long long amountCents,
                                                          int cashSessionId, const QString& note)
{
    DeviceOpResult result;
    if (rejectWithoutIdentity(&result)) {
        return result;
    }
    if (amountCents <= 0) {
        result.error = QStringLiteral("payment amount must be positive");
        return result;
    }
    if (customerId <= 0) {
        result.error = QStringLiteral("customer id is invalid");
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

    core::Payment payment;
    payment.customerId = customerId;
    payment.amountCents = amountCents;
    payment.createdAt = QDateTime::currentDateTime();
    payment.note = note;
    const int paymentId = m_payments.insert(payment);
    if (paymentId == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
    }

    QString movementError;
    const std::optional<int> movementId = cashDrawer::recordMoneyIn(
        m_db, session->id, core::cashMovementType::kCustomerPayment, amountCents,
        note, QStringLiteral("the customer payment"),
        QStringLiteral("payment"), paymentId, &movementError);
    if (!movementId.has_value()) {
        m_db.rollback();
        result.error = movementError;
        return result;
    }

    QString error;
    QVector<core::SaleItem> emptyItems;
    const int opId = mintAndEnqueue(core::SyncOpType::CustomerPayment, customerId, amountCents, emptyItems,
                                    note, &error);
    if (opId == 0) {
        m_db.rollback();
        result.error = error;
        return result;
    }

    if (!writeDeviceAudit(m_db, m_deviceId, QStringLiteral("customer_payment"), paymentId)) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the payment could not be recorded in the audit log")
            : m_db.lastError();
        return result;
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.opId = opId;
    result.entityId = paymentId;
    result.totalCents = amountCents;
    return result;
}

} // namespace app::data