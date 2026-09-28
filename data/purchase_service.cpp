#include "purchase_service.h"

#include <QDateTime>

#include "date_utils.h"

namespace app::data {

PurchaseService::PurchaseService(Database& db, PurchaseRepository& purchases, PurchaseItemRepository& items,
                                 ProductRepository& products, StockMovementRepository& stockMovements,
                                 SupplierRepository& suppliers, SupplierTransactionRepository& supplierTxs)
    : m_db(db)
    , m_purchases(purchases)
    , m_items(items)
    , m_products(products)
    , m_stockMovements(stockMovements)
    , m_suppliers(suppliers)
    , m_supplierTxs(supplierTxs)
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
    for (const core::PurchaseItem& item : items) {
        if (item.quantity <= 0) {
            result.error = QStringLiteral("quantity must be positive");
            return result;
        }
    }

    // The lines are the truth: a header whose total disagrees with them is a
    // typo, and storing the typo is what makes the supplier balance wrong.
    long long totalCents = 0;
    for (const core::PurchaseItem& item : items) {
        totalCents += item.totalCents;
    }

    core::Purchase header = purchase;
    if (totalCents != header.totalCents) {
        header.totalCents = totalCents;
    }
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
    const int purchaseId = m_purchases.insert(header);
    if (purchaseId == 0) {
        m_db.rollback();
        result.error = stepError(m_db, QStringLiteral("inserting the purchase"));
        return result;
    }

    // D. One line at a time inside the transaction this service owns.
    // PurchaseItemRepository::insertAll() opens a transaction of its own, and the
    // SQLite driver refuses a nested BEGIN (returning false) while its COMMIT
    // would end the surrounding transaction early, so calling it here would
    // either insert nothing or commit a half-written purchase.
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

    // The stock and the debt are two sides of one event, so they are written
    // together or not at all. A negative amount is money leaving the supplier.
    // TODO Phase 3: migrate to supplier_payments table
    if (header.paidCents > 0) {
        core::SupplierTransaction payment;
        payment.supplierId = header.supplierId;
        payment.amountCents = -header.paidCents;
        payment.createdAt = QDateTime::currentDateTime();
        payment.note = QStringLiteral("Payment for Purchase #%1").arg(purchaseId);
        if (m_supplierTxs.insert(payment) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting the supplier payment"));
            return result;
        }
    }
    if (header.addToStock) {
        core::SupplierTransaction debt;
        debt.supplierId = header.supplierId;
        debt.amountCents = header.totalCents;
        debt.createdAt = QDateTime::currentDateTime();
        debt.note = QStringLiteral("Purchase #%1").arg(purchaseId);
        if (m_supplierTxs.insert(debt) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting the supplier debt"));
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
