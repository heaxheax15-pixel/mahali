#include "supplier_payment_service.h"

#include <QSqlQuery>

#include "audit_log_repository.h"
#include "cash_drawer.h"
#include "date_utils.h"
#include "format_utils.h"

namespace app::data {

SupplierPaymentService::SupplierPaymentService(Database& db,
                                               SupplierPaymentRepository& payments,
                                               SupplierRepository& suppliers,
                                               PurchaseRepository& purchases)
    : m_db(db)
    , m_payments(payments)
    , m_suppliers(suppliers)
    , m_purchases(purchases)
{
}

namespace {

// The repository already recorded the driver message, so it is preferred; this
// only speaks up when the call failed without saying why, which would otherwise
// reach the operator as a blank reason.
QString stepError(const Database& db, const QString& step)
{
    const QString reason = db.lastError();
    if (!reason.isEmpty()) {
        return reason;
    }
    return QStringLiteral("SupplierPaymentService::recordPayment failed at %1").arg(step);
}

} // namespace

SupplierPaymentResult SupplierPaymentService::recordPayment(int supplierId,
                                                            std::optional<int> purchaseId,
                                                            long long amountCents,
                                                            const QString& paidAt,
                                                            const QString& note,
                                                            core::SupplierPaymentMethod method,
                                                            std::optional<int> cashSessionId)
{
    SupplierPaymentResult result;

    // A. The checks that cost nothing, before the transaction is opened. A
    // negative payment would be read as money owed to the supplier rather than
    // paid to them, which is the opposite of what the caller meant.
    if (amountCents <= 0) {
        result.error = QStringLiteral("amount must be positive");
        return result;
    }
    if (!m_suppliers.findById(supplierId).has_value()) {
        result.error = QStringLiteral("supplier not found");
        return result;
    }
    // Cash is the only method that reaches the drawer, so it is the only one that
    // needs a session to name. Settling the account or paying through the bank
    // moves the supplier's balance and leaves the till as it is, and refusing
    // those for want of a session would stop a shop recording how it really paid.
    if (core::isCashPayment(method) && !cashSessionId.has_value()) {
        result.error = QStringLiteral(
            "a cash payment to a supplier has to be booked against an open cash session, so that the money "
            "leaving the drawer is counted. Choose cash with a session open, or record it as credit or bank.");
        return result;
    }

    // A payment filed against an invoice is money for that invoice, so the two
    // have to agree on the supplier. Catching it here keeps a payment for one
    // supplier's invoice from quietly reducing another supplier's balance.
    if (purchaseId.has_value()) {
        const std::optional<core::Purchase> purchase = m_purchases.findById(*purchaseId);
        if (!purchase.has_value()) {
            result.error = QStringLiteral("purchase %1 not found").arg(*purchaseId);
            return result;
        }
        if (purchase->supplierId != supplierId) {
            result.error = QStringLiteral("purchase does not belong to supplier");
            return result;
        }
        // And it is money for what is left of that invoice: settling it and then
        // some would leave a credit that the balance reports as a debt nobody can
        // explain.
        long long settledCents = 0;
        for (const core::SupplierPayment& payment : m_payments.findByPurchaseId(*purchaseId)) {
            settledCents += payment.amountCents;
        }
        const long long remainingCents = purchase->totalCents - settledCents;
        if (amountCents > remainingCents) {
            result.error = QStringLiteral("Le montant dépasse le reste à payer sur cette facture (%1)")
                               .arg(ui::formatMoney(remainingCents));
            return result;
        }
    }

    // A general payment is deliberately not capped here. An advance to a
    // supplier the shop owes nothing yet is a real thing a shop does — goods are
    // paid for before they arrive — and refusing it would mean the balance can
    // never show a supplier in credit. The payment form still checks the figure
    // against the balance, but that is there to catch a mistyped amount, not to
    // make advances impossible.

    // B.
    if (!m_db.beginTransaction()) {
        result.error = stepError(m_db, QStringLiteral("opening the transaction"));
        return result;
    }

    // C.
    core::SupplierPayment payment;
    payment.supplierId = supplierId;
    payment.purchaseId = purchaseId;
    payment.amountCents = amountCents;
    payment.paidAt = paidAt.isEmpty() ? nowIso() : paidAt;
    payment.method = method;
    payment.note = note;
    payment.createdAt = nowIso();

    // D.
    const int paymentId = m_payments.insert(payment);
    if (paymentId == 0) {
        m_db.rollback();
        result.error = stepError(m_db, QStringLiteral("inserting the supplier payment"));
        return result;
    }

    // E. The drawer, for a payment that came out of it. This is the write that was
    // missing: the payment settled the supplier's balance and nothing else, so
    // money left the drawer without a trace of it and the session reconciled as
    // though it had never been paid. Inside the transaction above, so the drawer
    // and the payment cannot come apart.
    if (core::isCashPayment(method)) {
        QString sessionError;
        // Read after the transaction opens: the session may have been closed by
        // another window since the form was filled in, and a payment booked
        // against a closed till would land in a session nobody is counting.
        const std::optional<core::CashSession> session =
            cashDrawer::stillOpen(m_db, *cashSessionId, QStringLiteral("a cash payment to a supplier"),
                                  &sessionError);
        if (!session.has_value()) {
            m_db.rollback();
            result.error = sessionError;
            return result;
        }

        QString movementError;
        const std::optional<int> movementId = cashDrawer::recordMoneyOut(
            m_db, session->id, core::cashMovementType::kSupplierPayment, amountCents,
            note.isEmpty() ? QStringLiteral("Supplier #%1").arg(supplierId) : note,
            QStringLiteral("the payment to supplier #%1").arg(supplierId),
            QStringLiteral("supplier_payment"), paymentId, &movementError);
        if (!movementId.has_value()) {
            m_db.rollback();
            result.error = movementError;
            return result;
        }
    }

    // Inside the transaction, so the audit line and the payment it describes
    // commit or roll back together. Written by the service rather than the form
    // that asked for it: a payment the operator saw accepted but that was rolled
    // back would otherwise leave an audit row saying the supplier was paid.
    AuditLogRepository audit(m_db);
    if (audit.record(QStringLiteral("supplier_payment"),
                     QStringLiteral("payment %1, supplier %2, %3")
                         .arg(paymentId)
                         .arg(supplierId)
                         .arg(core::supplierPaymentMethodName(method)))
        == 0) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the payment could not be recorded in the audit log")
            : m_db.lastError();
        return result;
    }

    // Z. Database::commit() rolls the transaction back itself when it fails, so
    // the connection stays usable for the next payment.
    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.paymentId = paymentId;
    return result;
}

SupplierPaymentResult SupplierPaymentService::reversePayment(int paymentId, std::optional<int> cashSessionId)
{
    SupplierPaymentResult result;

    const auto original = m_payments.findById(paymentId);
    if (!original.has_value()) {
        result.error = QStringLiteral("payment not found");
        return result;
    }
    if (original->reversedId != 0) {
        result.error = QStringLiteral("already reversed");
        return result;
    }

    // A second reversal is prevented by the service guard above and by the
    // partial unique index on reversed_id (migration 3). Written here inside
    // the transaction so the check and the row it guards commit as one.
    if (!m_db.beginTransaction()) {
        result.error = stepError(m_db, QStringLiteral("opening the transaction"));
        return result;
    }

    // The supplier's balance is the sum of all payments (including reversals).
    // Writing a negative row against the original restores it exactly.
    core::SupplierPayment reversal;
    reversal.supplierId = original->supplierId;
    reversal.purchaseId = original->purchaseId;
    reversal.amountCents = -original->amountCents;
    reversal.paidAt = nowIso();
    reversal.method = original->method;
    reversal.note = QStringLiteral("reversal of #%1").arg(paymentId);
    reversal.createdAt = nowIso();
    reversal.reversedId = paymentId;
    const int reversalId = m_payments.insert(reversal);
    if (reversalId == 0) {
        m_db.rollback();
        result.error = stepError(m_db, QStringLiteral("inserting the reversal"));
        return result;
    }

    // Cash originally left the drawer, so the reversal puts it back.
    if (core::isCashPayment(original->method)) {
        QString sessionError;
        if (!cashSessionId.has_value()) {
            m_db.rollback();
            result.error = QStringLiteral(
                "a cash reversal needs the session the money returns to");
            return result;
        }
        const std::optional<core::CashSession> session =
            cashDrawer::stillOpen(m_db, *cashSessionId, QStringLiteral("a cash payment reversal"),
                                  &sessionError);
        if (!session.has_value()) {
            m_db.rollback();
            result.error = sessionError;
            return result;
        }

        QString movementError;
        const std::optional<int> movementId = cashDrawer::recordMoneyIn(
            m_db, session->id, core::cashMovementType::kSupplierPaymentReversal, original->amountCents,
            reversal.note,
            QStringLiteral("reversal of payment #%1").arg(paymentId),
            QStringLiteral("supplier_payment"), paymentId, &movementError);
        if (!movementId.has_value()) {
            m_db.rollback();
            result.error = movementError;
            return result;
        }
    }

    // Audit the reversal inside the same transaction.
    AuditLogRepository audit(m_db);
    if (audit.record(QStringLiteral("supplier_payment_reversal"),
                     QStringLiteral("payment %1, supplier %2").arg(reversalId).arg(original->supplierId))
        == 0) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the reversal could not be recorded in the audit log")
            : m_db.lastError();
        return result;
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.paymentId = reversalId;
    return result;
}

QVector<SupplierPaymentService::UnpaidInvoice> SupplierPaymentService::unpaidInvoicesFor(int supplierId) const
{
    QVector<UnpaidInvoice> unpaid;

    // One statement for the whole list: the total paid against each invoice is
    // folded in by the join, so asking for the outstanding invoices is a single
    // read instead of one query per invoice. The LEFT JOIN keeps invoices that
    // were never paid on, which is the common case.
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "SELECT p.id, p.total_cents - COALESCE(SUM(sp.amount_cents), 0) AS remaining "
        "FROM purchases p "
        "LEFT JOIN supplier_payments sp ON sp.purchase_id = p.id "
        "WHERE p.supplier_id = ? "
        "GROUP BY p.id "
        "HAVING remaining > 0 "
        "ORDER BY p.purchased_at ASC"));
    query.addBindValue(supplierId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierPaymentService::unpaidInvoicesFor"));
        return unpaid;
    }

    // Only the ids and the outstanding amounts come from the query; the invoices
    // themselves are read through the repository, which is where the mapping
    // from row to core::Purchase lives.
    QVector<std::pair<int, long long>> outstanding;
    while (query.next()) {
        outstanding.push_back({query.value(0).toInt(), query.value(1).toLongLong()});
    }

    unpaid.reserve(outstanding.size());
    for (const auto& [purchaseId, remainingCents] : outstanding) {
        const std::optional<core::Purchase> purchase = m_purchases.findById(purchaseId);
        if (!purchase.has_value()) {
            // The row was there a moment ago, so this means it was deleted
            // underneath us. Skipping it leaves the rest of the list usable.
            continue;
        }
        unpaid.push_back({*purchase, remainingCents});
    }

    return unpaid;
}

long long SupplierPaymentService::balanceFor(int supplierId) const
{
    return m_suppliers.balanceCentsFor(supplierId);
}

} // namespace app::data
