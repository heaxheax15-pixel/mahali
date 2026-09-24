#include "customer_transaction_item_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

CustomerTransactionItemRepository::CustomerTransactionItemRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::CustomerTransactionItem itemFromQuery(const QSqlQuery& query)
{
    core::CustomerTransactionItem item;
    item.id = query.value(0).toInt();
    item.customerTransactionId = query.value(1).toInt();
    item.productId = query.value(2).toInt();
    item.quantity = query.value(3).toLongLong();
    item.unitPriceCents = query.value(4).toLongLong();
    item.unitCostCents = query.value(5).toLongLong();
    item.reversedId = query.value(6).toInt();
    return item;
}

} // namespace

std::vector<core::CustomerTransactionItem> CustomerTransactionItemRepository::findByTransactionId(
    int transactionId) const
{
    std::vector<core::CustomerTransactionItem> items;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT id, customer_transaction_id, product_id, quantity, unit_price_cents, unit_cost_cents, "
                       "reversed_id FROM customer_transaction_items WHERE customer_transaction_id = ?"));
    query.addBindValue(transactionId);
    if (!query.exec()) {
        return items;
    }
    while (query.next()) {
        items.push_back(itemFromQuery(query));
    }
    return items;
}

int CustomerTransactionItemRepository::insert(const core::CustomerTransactionItem& item)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO customer_transaction_items "
                       "(customer_transaction_id, product_id, quantity, unit_price_cents, unit_cost_cents, reversed_id) "
                       "VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(item.customerTransactionId);
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