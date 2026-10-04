#include "sale_item_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

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
    // unit_kind and pieces_consumed are read after every pre-existing column so
    // the indices above keep meaning what they meant. Both are needed and neither
    // replaces the other: quantity is the number of units the cashier sold and is
    // what the price is counted by, while pieces_consumed is how many pieces left
    // the shelf and is what the cost and the stock movement are counted from. One
    // carton line is 1 of the first and 24 of the second.
    item.unitKind = query.value(7).toString();
    // toLongLong, not toInt: pieces_consumed is long long in the struct because it
    // is multiplied by unit_cost_cents in the COGS guard, and reading it back
    // through a 32-bit conversion would truncate the operand that guard checks.
    item.piecesConsumed = query.value(8).toLongLong();
    return item;
}

} // namespace

std::vector<core::SaleItem> SaleItemRepository::findBySaleId(int saleId) const
{
    std::vector<core::SaleItem> items;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT id, sale_id, product_id, quantity, unit_price_cents, unit_cost_cents, reversed_id, "
                       "unit_kind, pieces_consumed "
                       "FROM sale_items WHERE sale_id = ?"));
    query.addBindValue(saleId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SaleItemRepository::findBySaleId"));
        return items;
    }
    while (query.next()) {
        items.push_back(itemFromQuery(query));
    }
    return items;
}

std::map<int, long long> SaleItemRepository::findQuantitiesBySaleIds(
    const std::vector<int>& saleIds) const
{
    std::map<int, long long> quantities;
    if (saleIds.empty()) {
        return quantities;
    }

    // Placeholders built from the id list rather than a literal "IN (1,2,3)": the
    // list length is not known at compile time, and binding each value keeps it
    // out of the SQL text.
    QString placeholders;
    placeholders.reserve(static_cast<int>(saleIds.size()) * 2 - 1);
    placeholders += QStringLiteral("?");
    for (std::size_t i = 1; i < saleIds.size(); ++i) {
        placeholders += QStringLiteral(",?");
    }

    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT sale_id, SUM(quantity) FROM sale_items "
                                 "WHERE sale_id IN (%1) GROUP BY sale_id")
                      .arg(placeholders));
    for (const int saleId : saleIds) {
        query.addBindValue(saleId);
    }
    if (!query.exec()) {
        m_db.recordError(query.lastError(),
                         QStringLiteral("SaleItemRepository::findQuantitiesBySaleIds"));
        return quantities;
    }
    while (query.next()) {
        quantities.emplace(query.value(0).toInt(), query.value(1).toLongLong());
    }
    return quantities;
}

int SaleItemRepository::insert(const core::SaleItem& item)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO sale_items "
                       "(sale_id, product_id, quantity, unit_price_cents, unit_cost_cents, reversed_id, "
                       "unit_kind, pieces_consumed) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(item.saleId);
    query.addBindValue(item.productId);
    query.addBindValue(item.quantity);
    query.addBindValue(item.unitPriceCents);
    query.addBindValue(item.unitCostCents);
    query.addBindValue(item.reversedId);
    // Both columns are always written, never left to the column DEFAULT, so a row
    // written by this repository is as complete as one written by the migration:
    // a caller that has not been taught about cartons yet still gets a well-formed
    // line rather than a piece count of zero, which the cost path would read as
    // goods that cost nothing.
    query.addBindValue(item.unitKind.isEmpty() ? QStringLiteral("piece") : item.unitKind);
    // No fallback: resolveSaleItems is the single source that fills piecesConsumed,
    // and a fallback here would let the stored row disagree with the movement that
    // was already written.
    query.addBindValue(item.piecesConsumed);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SaleItemRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

} // namespace app::data