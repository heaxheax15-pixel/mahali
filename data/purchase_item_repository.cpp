#include "purchase_item_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

PurchaseItemRepository::PurchaseItemRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::PurchaseItem purchaseItemFromQuery(const QSqlQuery& query)
{
    core::PurchaseItem item;
    item.id = query.value(0).toInt();
    item.purchaseId = query.value(1).toInt();
    const int productId = query.value(2).toInt();
    if (productId != 0) {
        item.productId = productId;
    }
    item.description = query.value(3).toString();
    item.quantity = query.value(4).toLongLong();
    item.unit = query.value(5).toString();
    item.unitPriceCents = query.value(6).toLongLong();
    item.totalCents = query.value(7).toLongLong();
    return item;
}

} // namespace

QVector<core::PurchaseItem> PurchaseItemRepository::findByPurchase(int purchaseId) const
{
    QVector<core::PurchaseItem> items;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "SELECT id, purchase_id, product_id, description, quantity, unit, "
        "unit_price_cents, total_cents "
        "FROM purchase_items WHERE purchase_id = ? ORDER BY id"));
    query.addBindValue(purchaseId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("PurchaseItemRepository::findByPurchase"));
        return items;
    }
    while (query.next()) {
        items.push_back(purchaseItemFromQuery(query));
    }
    return items;
}

int PurchaseItemRepository::insert(const core::PurchaseItem& item)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "INSERT INTO purchase_items (purchase_id, product_id, description, "
        "quantity, unit, unit_price_cents, total_cents) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(item.purchaseId);
    if (item.productId.has_value()) {
        query.addBindValue(*item.productId);
    } else {
        query.addBindValue(QVariant());
    }
    query.addBindValue(item.description);
    query.addBindValue(item.quantity);
    query.addBindValue(item.unit);
    query.addBindValue(item.unitPriceCents);
    query.addBindValue(item.totalCents);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("PurchaseItemRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool PurchaseItemRepository::insertAll(int purchaseId, const QVector<core::PurchaseItem>& items)
{
    if (!m_db.beginTransaction()) {
        return false;
    }

    for (const core::PurchaseItem& item : items) {
        QSqlQuery query(m_db.handle());
        query.prepare(QStringLiteral(
            "INSERT INTO purchase_items (purchase_id, product_id, description, "
            "quantity, unit, unit_price_cents, total_cents) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)"));
        query.addBindValue(item.purchaseId);
        if (item.productId.has_value()) {
            query.addBindValue(*item.productId);
        } else {
            query.addBindValue(QVariant());
        }
        query.addBindValue(item.description);
        query.addBindValue(item.quantity);
        query.addBindValue(item.unit);
        query.addBindValue(item.unitPriceCents);
        query.addBindValue(item.totalCents);
        if (!query.exec()) {
            m_db.recordError(query.lastError(), QStringLiteral("PurchaseItemRepository::insertAll"));
            return false;
        }
    }

    if (!m_db.commit()) {
        return false;
    }
    return true;
}

bool PurchaseItemRepository::removeByPurchase(int purchaseId)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("DELETE FROM purchase_items WHERE purchase_id = ?"));
    query.addBindValue(purchaseId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("PurchaseItemRepository::removeByPurchase"));
        return false;
    }
    return query.numRowsAffected() > 0;
}

} // namespace app::data