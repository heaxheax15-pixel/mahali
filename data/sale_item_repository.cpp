#include "sale_item_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

SaleItemRepository::SaleItemRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::SaleItem itemFromQuery(const QSqlQuery& query)
{
    core::SaleItem item;
    item.id = query.value(0).toInt();
    item.saleId = query.value(1).toInt();
    item.productId = query.value(2).toInt();
    item.quantity = query.value(3).toLongLong();
    item.unitPriceCents = query.value(4).toLongLong();
    item.unitCostCents = query.value(5).toLongLong();
    item.reversedId = query.value(6).toInt();
    return item;
}

} // namespace

std::vector<core::SaleItem> SaleItemRepository::findBySaleId(int saleId) const
{
    std::vector<core::SaleItem> items;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT id, sale_id, product_id, quantity, unit_price_cents, unit_cost_cents, reversed_id "
                       "FROM sale_items WHERE sale_id = ?"));
    query.addBindValue(saleId);
    if (!query.exec()) {
        return items;
    }
    while (query.next()) {
        items.push_back(itemFromQuery(query));
    }
    return items;
}

int SaleItemRepository::insert(const core::SaleItem& item)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO sale_items "
                       "(sale_id, product_id, quantity, unit_price_cents, unit_cost_cents, reversed_id) "
                       "VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(item.saleId);
    query.addBindValue(item.productId);
    query.addBindValue(item.quantity);
    query.addBindValue(item.unitPriceCents);
    query.addBindValue(item.unitCostCents);
    query.addBindValue(item.reversedId);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

} // namespace app::data