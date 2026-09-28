#include "supplier_return_item_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

SupplierReturnItemRepository::SupplierReturnItemRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::SupplierReturnItem itemFromQuery(const QSqlQuery& query)
{
    core::SupplierReturnItem item;
    item.id = query.value(0).toInt();
    item.returnId = query.value(1).toInt();
    const int product = query.value(2).toInt();
    if (product != 0) {
        item.productId = product;
    }
    item.quantity = query.value(3).toLongLong();
    item.unitPriceCents = query.value(4).toLongLong();
    item.totalCents = query.value(5).toLongLong();
    return item;
}

const char* kItemColumns = "id, return_id, product_id, quantity, unit_price_cents, total_cents";

} // namespace

std::vector<core::SupplierReturnItem> SupplierReturnItemRepository::findByReturn(int returnId) const
{
    std::vector<core::SupplierReturnItem> items;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM supplier_return_items WHERE return_id = ? ORDER BY id")
                      .arg(QLatin1StringView(kItemColumns)));
    query.addBindValue(returnId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierReturnItemRepository::findByReturn"));
        return items;
    }
    while (query.next()) {
        items.push_back(itemFromQuery(query));
    }
    return items;
}

int SupplierReturnItemRepository::insert(const core::SupplierReturnItem& item)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("INSERT INTO supplier_return_items (return_id, product_id, quantity, "
                                 "unit_price_cents, total_cents) VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(item.returnId);
    if (item.productId.has_value()) {
        query.addBindValue(*item.productId);
    } else {
        query.addBindValue(QVariant());
    }
    // Every column here is a number, so there is no empty-string guard to make:
    // the NOT NULL constraints are already covered by the struct defaults.
    query.addBindValue(item.quantity);
    query.addBindValue(item.unitPriceCents);
    query.addBindValue(item.totalCents);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierReturnItemRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool SupplierReturnItemRepository::removeByReturn(int returnId)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("DELETE FROM supplier_return_items WHERE return_id = ?"));
    query.addBindValue(returnId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierReturnItemRepository::removeByReturn"));
        return false;
    }
    return query.numRowsAffected() > 0;
}

} // namespace app::data
