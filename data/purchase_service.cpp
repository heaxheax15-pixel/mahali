#include "purchase_service.h"

#include <QDateTime>

#include "date_utils.h"

namespace app::data {

PurchaseService::PurchaseService(Database& db, PurchaseRepository& purchases, PurchaseItemRepository& items,
                                 ProductRepository& products, StockMovementRepository& stockMovements,
                                 SupplierRepository& suppliers, SupplierPaymentRepository& supplierPayments)
    : m_db(db)
    , m_purchases(purchases)
    , m_items(items)
    , m_products(products)
    , m_stockMovements(stockMovements)
    , m_suppliers(suppliers)
    , m_supplierPayments(supplierPayments)
    , m_occasions(db)
    , m_settings(db)
    , m_occasionService(db, m_occasions, m_settings)
{
}

namespace {

// The repositories already recorded the driver message, so it is preferred; this
// only speaks up when the call failed without saying why, which would otherwise
// reach the operator as a blank reason.
QString stepError(const Database& db, const QString& step)
{
    const QString reason = db.lastError();
    if (!reason.isEmpty()) {
        return reason;
    }
    return QStringLiteral("PurchaseService::recordPurchase failed at %1").arg(step);
}

} // namespace

PurchaseResult PurchaseService::recordPurchase(const core::Purchase& purchase,
                                               const QVector<core::PurchaseItem>& items)
{
    PurchaseResult result;

    // A. The checks that cost nothing, before the transaction is opened.
    if (purchase.supplierId <= 0) {
        result.error = QStringLiteral("supplier id is required");
        return result;
    }
    if (!m_suppliers.findById(purchase.supplierId).has_value()) {
        result.error = QStringLiteral("supplier %1 does not exist").arg(purchase.supplierId);
        return result;
    }
    if (items.isEmpty()) {
        result.error = QStringLiteral("purchase items are empty");
        return result;
    }
    // The lines are the truth for the sub-total: a header whose sub-total
    // disagrees with them is a typo, and storing the typo is what makes the
    // supplier balance wrong. It is added up here, in the same pass as the
    // quantity check, so the total is known before anything is written.
    long long subtotalCents = 0;
    for (const core::PurchaseItem& item : items) {
        if (item.quantity <= 0) {
            result.error = QStringLiteral("quantity must be positive");
            return result;
        }
        subtotalCents += item.totalCents;
    }
    // The VAT is the caller's, not the lines': it is charged on the invoice as a
    // whole rather than on any one line, so it cannot be worked out here and is
    // taken as it was given.
    const long long vatCents = purchase.vatCents;
    const long long totalCents = subtotalCents + vatCents;
    // Paying more than the invoice is worth would leave the supplier in credit
    // for money nobody owes them, and the balance would carry that credit as a
    // debt the operator cannot explain.
    if (purchase.paidCents > totalCents) {
        result.error = QStringLiteral("paid amount %1 exceeds the invoice total %2")
                           .arg(purchase.paidCents)
                           .arg(totalCents);
        return result;
    }

    // The three amounts are written from what was worked out above rather than
    // from whatever the caller sent, so they always add up in the database: an
    // invoice recorded with VAT is stored with it, and the supplier balance read
    // from total_cents is what is genuinely owed.
    core::Purchase header = purchase;
    header.subtotalCents = subtotalCents;
    header.vatCents = vatCents;
    header.totalCents = totalCents;
    if (header.purchasedAt.isEmpty()) {
        header.purchasedAt = nowIso();
    }
    if (header.createdAt.isEmpty()) {
        header.createdAt = nowIso();
    }
    // purchases.invoice_number and purchases.note are NOT NULL with no default
    // that a bound NULL would fall back to, so a caller that never filled them
    // in would be turned away by the driver instead of by the check above.
    if (header.invoiceNumber.isNull()) {
        header.invoiceNumber = QStringLiteral("");
    }
    if (header.note.isNull()) {
        header.note = QStringLiteral("");
    }

    // B.
    if (!m_db.beginTransaction()) {
        result.error = stepError(m_db, QStringLiteral("opening the transaction"));
        return result;
    }

    // C.
    // Stamped with whatever occasion is running now, so stock bought for an
    // event can be told apart from stock bought the rest of the time. The
    // column has been on purchases since 2a and was left unwritten until now; a
    // purchase with no occasion running keeps the NULL it already had. Read
    // after the transaction opens, so the purchase and the occasion it was
    // attributed to come from one snapshot.
    if (const std::optional<core::Occasion> occasion = m_occasionService.current()) {
        header.occasionId = occasion->id;
    }
    const int purchaseId = m_purchases.insert(header);
    if (purchaseId == 0) {
        m_db.rollback();
        result.error = stepError(m_db, QStringLiteral("inserting the purchase"));
        return result;
    }

    // D. One line at a time inside the transaction this service owns.
    // Items are inserted one at a time, inside this service's transaction,
    // so the whole purchase commits or rolls back as a single unit.
    QVector<core::PurchaseItem> lines = items;
    for (core::PurchaseItem& line : lines) {
        line.purchaseId = purchaseId;
    }
    for (const core::PurchaseItem& line : lines) {
        if (m_items.insert(line) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting a purchase item"));
            return result;
        }
    }

    // E.
    if (header.addToStock) {
        for (const core::PurchaseItem& line : lines) {
            if (!line.productId.has_value() || *line.productId <= 0) {
                continue;
            }
            const int productId = *line.productId;
            // Read before the movement lands: products.quantity only moves when
            // the trg_stock_after_insert trigger reacts to that insert, and the
            // average cost needs the level the stock was at beforehand.
            const std::optional<core::Product> product = m_products.findById(productId);
            if (!product.has_value()) {
                m_db.rollback();
                result.error =
                    QStringLiteral("purchase item references product %1, which does not exist").arg(productId);
                return result;
            }

            core::StockMovement movement;
            movement.productId = productId;
            movement.delta = line.quantity;
            movement.reason = QStringLiteral("purchase");
            movement.reference = QStringLiteral("Purchase #%1").arg(purchaseId);
            movement.createdAt = QDateTime::currentDateTime();
            if (m_stockMovements.insert(movement) == 0) {
                m_db.rollback();
                result.error = stepError(m_db, QStringLiteral("inserting a stock movement"));
                return result;
            }

            if (!m_products.updateAverageCost(productId, product->quantity, product->costPriceCents, line.quantity,
                                              line.unitPriceCents)) {
                m_db.rollback();
                result.error = stepError(m_db, QStringLiteral("updating the average cost"));
                return result;
            }
        }
    }

    // What was paid on the invoice is a payment against it, written next to the
    // stock it pays for so both land or neither does. The invoice total itself
    // is already in purchases, which is what the balance is read from; adding a
    // second row for it here would count the debt twice.
    if (header.paidCents > 0) {
        core::SupplierPayment payment;
        payment.supplierId = header.supplierId;
        payment.purchaseId = purchaseId;
        payment.amountCents = header.paidCents;
        payment.paidAt = header.purchasedAt;
        payment.note = QStringLiteral("Purchase #%1").arg(purchaseId);
        payment.createdAt = nowIso();
        if (m_supplierPayments.insert(payment) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting the supplier payment"));
            return result;
        }
    }

    // Z. Database::commit() rolls the transaction back itself when it fails, so
    // the connection stays usable for the next purchase.
    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.purchaseId = purchaseId;
    return result;
}

} // namespace app::data
