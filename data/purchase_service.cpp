#include "purchase_service.h"

#include <QDateTime>
#include <QMap>
#include <QSqlQuery>

#include "date_utils.h"
#include "cash_drawer.h"
#include "audit_log_repository.h"
#include "core/cash_movement.h"

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
                                               const QVector<core::PurchaseItem>& items,
                                               core::SupplierPaymentMethod method,
                                               std::optional<int> cashSessionId)
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
    // Only money that comes out of the drawer needs a drawer to come out of.
    // An invoice paid in full on the account, or through the bank, settles the
    // supplier and leaves the till alone; an invoice unpaid settles nothing and
    // has no money to move at all. So a session is required in exactly one case,
    // and saying so up front beats writing a payment nobody can reconcile.
    if (purchase.paidCents > 0 && core::isCashPayment(method) && !cashSessionId.has_value()) {
        result.error = QStringLiteral(
            "this invoice was paid out of the drawer, so it has to be booked against an open cash session. "
            "Open a session and enter the paid amount again, or record the invoice as paid on the account.");
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
        payment.method = method;
        payment.isPurchaseInitialPayment = true;
        payment.note = QStringLiteral("Purchase #%1").arg(purchaseId);
        payment.createdAt = nowIso();
        if (m_supplierPayments.insert(payment) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting the supplier payment"));
            return result;
        }

        // And, when that payment came out of the drawer, the drawer gave up the
        // same amount. Without this row the session reconciles as though the
        // invoice had been paid on the account, and the shop closes its day
        // short by exactly what it paid.
        if (core::isCashPayment(method)) {
            QString sessionError;
            const std::optional<core::CashSession> session = cashDrawer::stillOpen(
                m_db, *cashSessionId, QStringLiteral("a cash payment for this invoice"), &sessionError);
            if (!session.has_value()) {
                m_db.rollback();
                result.error = sessionError;
                return result;
            }
            QString movementError;
            const std::optional<int> movementId = cashDrawer::recordMoneyOut(
                m_db, session->id, core::cashMovementType::kSupplierPayment, header.paidCents,
                QStringLiteral("Purchase #%1").arg(purchaseId),
                QStringLiteral("the payment for purchase #%1").arg(purchaseId),
                QStringLiteral("purchase"), purchaseId, &movementError);
            if (!movementId.has_value()) {
                m_db.rollback();
                result.error = movementError;
                return result;
            }
        }
    }

    // Inside the transaction: a purchase that raised the supplier's balance and
    // took stock off the shelf, and a paid part of it out of the drawer, has to be
    // audited by whoever paid for it. Written here, the entry rolls back with the
    // purchase if any of it goes wrong, and a purchase that never happened leaves
    // no line claiming that it did.
    AuditLogRepository audit(m_db);
    if (audit.record(QStringLiteral("purchase"),
                     QStringLiteral("purchase %1, supplier %2")
                         .arg(purchaseId)
                         .arg(purchase.supplierId))
        == 0) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the purchase could not be recorded in the audit log")
            : m_db.lastError();
        return result;
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

PurchaseResult PurchaseService::voidPurchase(int purchaseId, std::optional<int> cashSessionId)
{
    PurchaseResult result;

    const auto original = m_purchases.findById(purchaseId);
    if (!original.has_value()) {
        result.error = QStringLiteral("purchase not found");
        return result;
    }
    // purchases.reversed_id is 0 on every ordinary row; a non-zero value means
    // this one has already been voided.
    if (original->reversedId != 0) {
        result.error = QStringLiteral("already voided");
        return result;
    }

    // 1) Guard: a line on this invoice cannot be voided once the goods have
    // moved again. stock_movements.id is monotonic, so the last movement this
    // purchase made for a product is the cut-off: anything above it is a later
    // sale, adjustment, return or purchase of the same product, and taking the
    // stock back now would rewrite a cost basis the shelf no longer agrees with.
    // The reference is the exact "Purchase #<id>" recordPurchase wrote, so
    // another supplier's invoice touching the same product still counts as later.
    const auto originalItems = m_items.findByPurchase(purchaseId);
    const QString purchaseReference = QStringLiteral("Purchase #%1").arg(purchaseId);
    QMap<int, long long> quantitiesByProduct;
    for (const core::PurchaseItem& originalItem : originalItems) {
        if (!original->addToStock || !originalItem.productId.has_value() || *originalItem.productId <= 0) {
            continue;
        }
        quantitiesByProduct[*originalItem.productId] += originalItem.quantity;
    }
    for (auto it = quantitiesByProduct.cbegin(); it != quantitiesByProduct.cend(); ++it) {
        const int productId = it.key();

        long long lastOwnMovementId = 0;
        QSqlQuery own(m_db.handle());
        own.prepare(QStringLiteral(
            "SELECT MAX(id) FROM stock_movements "
            "WHERE product_id = ? AND reason = 'purchase' AND reference = ?"));
        own.addBindValue(productId);
        own.addBindValue(purchaseReference);
        if (!own.exec()) {
            result.error = stepError(m_db, QStringLiteral("finding the purchase stock movement"));
            return result;
        }
        if (own.next() && !own.value(0).isNull()) {
            lastOwnMovementId = own.value(0).toLongLong();
        }
        if (lastOwnMovementId <= 0) {
            continue;
        }

        QSqlQuery later(m_db.handle());
        later.prepare(QStringLiteral(
            "SELECT 1 FROM stock_movements WHERE product_id = ? AND id > ? LIMIT 1"));
        later.addBindValue(productId);
        later.addBindValue(lastOwnMovementId);
        if (!later.exec()) {
            result.error = stepError(m_db, QStringLiteral("checking later stock movements"));
            return result;
        }
        if (later.next()) {
            result.error = QStringLiteral(
                "cannot void: product %1 has moved stock since this purchase").arg(productId);
            return result;
        }

        const auto product = m_products.findById(productId);
        if (!product.has_value() || product->quantity < it.value()) {
            result.error = QStringLiteral(
                "cannot void: product %1 does not have enough stock to reverse this purchase").arg(productId);
            return result;
        }
    }

    // Only the exact row written by recordPurchase is exempt. Timestamp equality
    // is not identity: a later payment can be recorded in the same clock tick.
    const auto supplierPayments = m_supplierPayments.findByPurchaseId(purchaseId);
    for (const auto& sp : supplierPayments) {
        if (sp.reversedId != 0 || sp.isPurchaseInitialPayment) {
            continue;
        }
        result.error = QStringLiteral(
            "cannot void: this invoice has a later payment of %1")
                           .arg(QString::number(sp.amountCents / 100.0, 'f', 2));
        return result;
    }

    if (!m_db.beginTransaction()) {
        result.error = stepError(m_db, QStringLiteral("opening the transaction"));
        return result;
    }

    // The supplier's balance is the sum of purchase totals (including voids).
    // A negative header restores it exactly.
    core::Purchase reversal = *original;
    reversal.id = 0;
    reversal.totalCents = -original->totalCents;
    reversal.subtotalCents = -original->subtotalCents;
    reversal.vatCents = -original->vatCents;
    reversal.paidCents = 0;
    reversal.reversedId = purchaseId;
    if (reversal.purchasedAt.isEmpty()) {
        reversal.purchasedAt = nowIso();
    }
    if (reversal.createdAt.isEmpty()) {
        reversal.createdAt = nowIso();
    }
    const int reversalId = m_purchases.insert(reversal);
    if (reversalId == 0) {
        m_db.rollback();
        result.error = stepError(m_db, QStringLiteral("inserting the void purchase"));
        return result;
    }

    // Negative items, each linked to the original, so the stock that comes back
    // is item-by-item identical to what went out.
    for (const core::PurchaseItem& originalItem : originalItems) {
        core::PurchaseItem reversalItem = originalItem;
        reversalItem.id = 0;
        reversalItem.purchaseId = reversalId;
        reversalItem.quantity = -originalItem.quantity;
        reversalItem.totalCents = -originalItem.totalCents;
        reversalItem.reversedId = originalItem.id;
        if (m_items.insert(reversalItem) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting the void purchase item"));
            return result;
        }

// The goods go back the way they came out: the movement is the same size with
            // the opposite sign, so the trigger takes the stock off the shelf
            // by exactly what the purchase put on it. PMP is not recomputed:
            // changing a running average retroactively would make the ledger
            // disagree with the stock on the shelf, which is what the audit
            // catches.
            if (original->addToStock && originalItem.productId.has_value() && *originalItem.productId > 0) {
                const int productId = *originalItem.productId;
                core::StockMovement movement;
                movement.productId = productId;
                movement.delta = -originalItem.quantity;
            movement.reason = QStringLiteral("purchase_void");
            movement.reference = QStringLiteral("Void of Purchase #%1").arg(purchaseId);
            movement.createdAt = QDateTime::currentDateTime();
            // Pointed at the movement it undoes, so the pair reads as one
            // movement and its mirror rather than two unrelated rows.
            QSqlQuery originalMovement(m_db.handle());
            originalMovement.prepare(QStringLiteral(
                "SELECT MAX(id) FROM stock_movements "
                "WHERE product_id = ? AND reason = 'purchase' AND reference = ?"));
            originalMovement.addBindValue(productId);
            originalMovement.addBindValue(purchaseReference);
            if (originalMovement.exec() && originalMovement.next() && !originalMovement.value(0).isNull()) {
                movement.reversedId = originalMovement.value(0).toInt();
            }
            if (m_stockMovements.insert(movement) == 0) {
                m_db.rollback();
                result.error = stepError(m_db, QStringLiteral("inserting the stock movement"));
                return result;
            }
        }
    }

    // If the original had a cash payment, that payment must be reversed too: the
    // money returns to the drawer, and the supplier payment row is cancelled.
    if (original->paidCents > 0 && core::isCashPayment(original->method)) {
        if (supplierPayments.empty()) {
            m_db.rollback();
            result.error = QStringLiteral("the cash payment for this purchase could not be found");
            return result;
        }
        // The initial payment (the one from the original purchase) has paidAt
        // matching the original purchase's purchasedAt.
        int paymentId = -1;
        for (const auto& sp : supplierPayments) {
            if (sp.isPurchaseInitialPayment && sp.reversedId == 0) {
                paymentId = sp.id;
                break;
            }
        }
        if (paymentId == -1) {
            m_db.rollback();
            result.error = QStringLiteral("the cash payment for this purchase could not be identified");
            return result;
        }
        // Reuse the supplier payment service's reversal logic through the
        // repository directly, to keep it inside this transaction. The
        // supplier_payment_reversal row will be written with the same method and
        // a positive cash movement back into the drawer.
        core::SupplierPayment paymentReversal;
        paymentReversal.supplierId = original->supplierId;
        paymentReversal.purchaseId = purchaseId;
        paymentReversal.amountCents = -original->paidCents;
        paymentReversal.paidAt = nowIso();
        paymentReversal.method = original->method;
        paymentReversal.note = QStringLiteral("reversal of payment for Purchase #%1").arg(purchaseId);
        paymentReversal.createdAt = nowIso();
        paymentReversal.reversedId = paymentId;
        if (m_supplierPayments.insert(paymentReversal) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting the payment reversal"));
            return result;
        }

        QString sessionError;
        if (!cashSessionId.has_value()) {
            m_db.rollback();
            result.error = QStringLiteral("a void of a cash purchase needs the session the money returns to");
            return result;
        }
        const std::optional<core::CashSession> session =
            cashDrawer::stillOpen(m_db, *cashSessionId, QStringLiteral("a void of a cash purchase"),
                                  &sessionError);
        if (!session.has_value()) {
            m_db.rollback();
            result.error = sessionError;
            return result;
        }
        QString movementError;
        const std::optional<int> movementId = cashDrawer::recordMoneyIn(
            m_db, session->id, core::cashMovementType::kSupplierPaymentReversal, original->paidCents,
            QStringLiteral("Void of Purchase #%1").arg(purchaseId),
            QStringLiteral("the reversal of payment for purchase #%1").arg(purchaseId),
            QStringLiteral("purchase"), purchaseId, &movementError);
        if (!movementId.has_value()) {
            m_db.rollback();
            result.error = movementError;
            return result;
        }
    }

    // Audit the void inside the same transaction.
    AuditLogRepository audit(m_db);
    if (audit.record(QStringLiteral("purchase_void"),
                     QStringLiteral("purchase %1, supplier %2").arg(reversalId).arg(original->supplierId))
        == 0) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the void could not be recorded in the audit log")
            : m_db.lastError();
        return result;
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.purchaseId = reversalId;
    return result;
}

} // namespace app::data
