#include "supplier_return_service.h"

#include <QDateTime>

#include "date_utils.h"

namespace app::data {

SupplierReturnService::SupplierReturnService(Database& db,
                                             SupplierReturnRepository& returns,
                                             SupplierReturnItemRepository& returnItems,
                                             SupplierRepository& suppliers,
                                             PurchaseRepository& purchases,
                                             ProductRepository& products,
                                             StockMovementRepository& stockMovements)
    : m_db(db)
    , m_returns(returns)
    , m_returnItems(returnItems)
    , m_suppliers(suppliers)
    , m_purchases(purchases)
    , m_products(products)
    , m_stockMovements(stockMovements)
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
    return QStringLiteral("SupplierReturnService failed at %1").arg(step);
}

} // namespace

SupplierReturnResult SupplierReturnService::recordLinkedReturn(int supplierId,
                                                                int purchaseId,
                                                                const QVector<core::SupplierReturnItem>& items,
                                                                const QString& returnedAt,
                                                                const QString& note)
{
    SupplierReturnResult result;

    // A. The checks that cost nothing, before the transaction is opened.
    if (!m_suppliers.findById(supplierId).has_value()) {
        result.error = QStringLiteral("supplier not found");
        return result;
    }

    // Crediting one supplier for goods returned against another supplier's
    // invoice would settle the wrong debt, so the two have to agree first.
    const std::optional<core::Purchase> purchase = m_purchases.findById(purchaseId);
    if (!purchase.has_value()) {
        result.error = QStringLiteral("purchase %1 not found").arg(purchaseId);
        return result;
    }
    if (purchase->supplierId != supplierId) {
        result.error = QStringLiteral("purchase does not belong to supplier");
        return result;
    }

    if (items.isEmpty()) {
        result.error = QStringLiteral("return items are empty");
        return result;
    }
    for (const core::SupplierReturnItem& item : items) {
        if (item.quantity <= 0) {
            result.error = QStringLiteral("quantity must be positive");
            return result;
        }
        if (item.unitPriceCents <= 0) {
            result.error = QStringLiteral("unit price must be positive");
            return result;
        }
    }

    // The lines are what the supplier is credited, so the header total is taken
    // from them rather than accepted from the caller.
    long long amountCents = 0;
    for (const core::SupplierReturnItem& item : items) {
        amountCents += item.totalCents;
    }

    // B.
    if (!m_db.beginTransaction()) {
        result.error = stepError(m_db, QStringLiteral("opening the transaction"));
        return result;
    }

    // C.
    core::SupplierReturn r;
    r.supplierId = supplierId;
    r.purchaseId = purchaseId;
    r.amountCents = amountCents;
    r.returnedAt = returnedAt.isEmpty() ? nowIso() : returnedAt;
    // A linked return always names its lines, and goods that went back to the
    // supplier are no longer on the shelves.
    r.removeFromStock = true;
    r.note = note;
    r.createdAt = nowIso();

    // D.
    const int returnId = m_returns.insert(r);
    if (returnId == 0) {
        m_db.rollback();
        result.error = stepError(m_db, QStringLiteral("inserting the supplier return"));
        return result;
    }

    // E. One line at a time, inside the transaction this service owns.
    QVector<core::SupplierReturnItem> lines = items;
    for (core::SupplierReturnItem& line : lines) {
        line.returnId = returnId;
    }
    for (const core::SupplierReturnItem& line : lines) {
        if (m_returnItems.insert(line) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting a return item"));
            return result;
        }
    }

    // F. The goods leave the shelves by the same mechanism that put them there,
    // so the level after the return is the level the trigger has just written.
    // The average cost is deliberately left alone: the goods were bought at the
    // price that was paid for them, so removing them changes the level and not
    // the cost. Recomputing it here would rewrite the cost of everything still
    // on the shelf, which is what the purchase average is for.
    for (const core::SupplierReturnItem& line : lines) {
        if (!line.productId.has_value() || *line.productId <= 0) {
            continue;
        }
        const int productId = *line.productId;

        core::StockMovement movement;
        movement.productId = productId;
        movement.delta = -line.quantity;
        movement.reason = QStringLiteral("supplier_return");
        movement.reference = QStringLiteral("Supplier Return #%1").arg(returnId);
        movement.createdAt = QDateTime::currentDateTime();
        if (m_stockMovements.insert(movement) == 0) {
            m_db.rollback();
            result.error = stepError(m_db, QStringLiteral("inserting a stock movement"));
            return result;
        }

        // Read after the movement landed, because that is the insert the trigger
        // reacted to. Going below zero is allowed and reported, not refused.
        const std::optional<core::Product> product = m_products.findById(productId);
        if (product.has_value() && product->quantity < 0 && result.warning.isEmpty()) {
            result.warning = QStringLiteral("Stock is negative for %1").arg(product->name);
        }
    }

    // Z. Database::commit() rolls the transaction back itself when it fails, so
    // the connection stays usable for the next return.
    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.returnId = returnId;
    return result;
}

SupplierReturnResult SupplierReturnService::recordGeneralReturn(int supplierId,
                                                                 long long amountCents,
                                                                 const QString& returnedAt,
                                                                 const QString& note)
{
    SupplierReturnResult result;

    // A. A credit that is not positive is not a credit, and would read as money
    // owed to the supplier once the balance subtracts it.
    if (!m_suppliers.findById(supplierId).has_value()) {
        result.error = QStringLiteral("supplier not found");
        return result;
    }
    if (amountCents <= 0) {
        result.error = QStringLiteral("amount must be positive");
        return result;
    }

    // B.
    if (!m_db.beginTransaction()) {
        result.error = stepError(m_db, QStringLiteral("opening the transaction"));
        return result;
    }

    // C. No invoice and no lines, so the stock is left alone: there is nothing
    // here that says which shelf the goods came off.
    core::SupplierReturn r;
    r.supplierId = supplierId;
    r.purchaseId = std::nullopt;
    r.amountCents = amountCents;
    r.returnedAt = returnedAt.isEmpty() ? nowIso() : returnedAt;
    r.removeFromStock = false;
    r.note = note;
    r.createdAt = nowIso();

    // D.
    const int returnId = m_returns.insert(r);
    if (returnId == 0) {
        m_db.rollback();
        result.error = stepError(m_db, QStringLiteral("inserting the supplier return"));
        return result;
    }

    // Z.
    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.returnId = returnId;
    return result;
}

} // namespace app::data
