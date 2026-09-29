#include "supplier_payment_service.h"

#include <QSqlQuery>

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
                                                            const QString& note)
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
    payment.note = note;
    payment.createdAt = nowIso();

    // D.
    const int paymentId = m_payments.insert(payment);
    if (paymentId == 0) {
        m_db.rollback();
        result.error = stepError(m_db, QStringLiteral("inserting the supplier payment"));
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
