#include "purchase_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

PurchaseRepository::PurchaseRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::Purchase purchaseFromQuery(const QSqlQuery& query)
{
    core::Purchase purchase;
    purchase.id = query.value(0).toInt();
    purchase.supplierId = query.value(1).toInt();
    purchase.invoiceNumber = query.value(2).toString();
    purchase.purchasedAt = query.value(3).toString();
    purchase.subtotalCents = query.value(4).toLongLong();
    purchase.vatCents = query.value(5).toLongLong();
    purchase.totalCents = query.value(6).toLongLong();
    purchase.paidCents = query.value(7).toLongLong();
    purchase.addToStock = query.value(8).toInt() != 0;
    purchase.note = query.value(9).toString();
    const int occasion = query.value(10).toInt();
    if (occasion != 0) {
        purchase.occasionId = occasion;
    }
    purchase.createdAt = query.value(11).toString();
    purchase.method = core::parseSupplierPaymentMethod(query.value(12).toString())
                          .value_or(core::SupplierPaymentMethod::Cash);
    purchase.reversedId = query.value(13).toInt();
    return purchase;
}

const char* kPurchaseColumns =
    "id, supplier_id, invoice_number, purchased_at, subtotal_cents, "
    "vat_cents, total_cents, paid_cents, method, add_to_stock, note, occasion_id, created_at, reversed_id";

} // namespace

std::optional<core::Purchase> PurchaseRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, supplier_id, invoice_number, purchased_at, "
                                 "subtotal_cents, vat_cents, total_cents, paid_cents, "
                                 "add_to_stock, note, occasion_id, created_at "
                                 "FROM purchases WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return purchaseFromQuery(query);
}

QVector<core::Purchase> PurchaseRepository::findBySupplier(int supplierId) const
{
    QVector<core::Purchase> purchases;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "SELECT id, supplier_id, invoice_number, purchased_at, "
        "subtotal_cents, vat_cents, total_cents, paid_cents, "
        "add_to_stock, note, occasion_id, created_at "
        "FROM purchases WHERE supplier_id = ? ORDER BY purchased_at DESC"));
    query.addBindValue(supplierId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("PurchaseRepository::findBySupplier"));
        return purchases;
    }
    while (query.next()) {
        purchases.push_back(purchaseFromQuery(query));
    }
    return purchases;
}

QVector<core::Purchase> PurchaseRepository::findBetween(const QString& fromIso, const QString& toIso) const
{
    QVector<core::Purchase> purchases;
    // A caller asking for "2026-03-31" means that whole day. The stored value is
    // a full timestamp, and "2026-03-31" sorts before "2026-03-31T10:00:00.000",
    // so the upper bound has to reach the last moment of the day or the invoices
    // of that day go missing from the range.
    const QString from = widenToDayStart(fromIso);
    const QString to = widenToDayEnd(toIso);

    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "SELECT id, supplier_id, invoice_number, purchased_at, "
        "subtotal_cents, vat_cents, total_cents, paid_cents, "
        "add_to_stock, note, occasion_id, created_at "
        "FROM purchases WHERE purchased_at >= ? AND purchased_at <= ? ORDER BY purchased_at"));
    query.addBindValue(from);
    query.addBindValue(to);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("PurchaseRepository::findBetween"));
        return purchases;
    }
    while (query.next()) {
        purchases.push_back(purchaseFromQuery(query));
    }
    return purchases;
}

QVector<core::Purchase> PurchaseRepository::findAll() const
{
    QVector<core::Purchase> purchases;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "SELECT id, supplier_id, invoice_number, purchased_at, "
        "subtotal_cents, vat_cents, total_cents, paid_cents, "
        "add_to_stock, note, occasion_id, created_at "
        "FROM purchases ORDER BY purchased_at DESC"));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("PurchaseRepository::findAll"));
        return purchases;
    }
    while (query.next()) {
        purchases.push_back(purchaseFromQuery(query));
    }
    return purchases;
}

int PurchaseRepository::insert(const core::Purchase& purchase)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "INSERT INTO purchases (supplier_id, invoice_number, purchased_at, "
        "subtotal_cents, vat_cents, total_cents, paid_cents, "
        "method, add_to_stock, note, occasion_id, created_at, reversed_id) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(purchase.supplierId);
    query.addBindValue(purchase.invoiceNumber.isNull() ? QStringLiteral("") : purchase.invoiceNumber);
    query.addBindValue(purchase.purchasedAt.isNull() ? QStringLiteral("") : purchase.purchasedAt);
    query.addBindValue(purchase.subtotalCents);
    query.addBindValue(purchase.vatCents);
    query.addBindValue(purchase.totalCents);
    query.addBindValue(purchase.paidCents);
    query.addBindValue(core::supplierPaymentMethodName(purchase.method));
    query.addBindValue(purchase.addToStock ? 1 : 0);
    query.addBindValue(purchase.note.isNull() ? QStringLiteral("") : purchase.note);
    if (purchase.occasionId.has_value()) {
        query.addBindValue(*purchase.occasionId);
    } else {
        query.addBindValue(QVariant());
    }
    query.addBindValue(purchase.createdAt.isNull() ? QStringLiteral("") : purchase.createdAt);
    query.addBindValue(purchase.reversedId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("PurchaseRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool PurchaseRepository::updateMeta(int id, long long paidCents, const QString& note, bool addToStock)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "UPDATE purchases SET paid_cents = ?, note = ?, add_to_stock = ? WHERE id = ?"));
    query.addBindValue(paidCents);
    query.addBindValue(note);
    query.addBindValue(addToStock ? 1 : 0);
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("PurchaseRepository::updateMeta"));
        return false;
    }
    return query.numRowsAffected() > 0;
}

} // namespace app::data