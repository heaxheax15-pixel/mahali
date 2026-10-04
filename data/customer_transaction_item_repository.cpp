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
    // Read after every pre-existing column, for the same reason as the cash sale
    // lines: quantity is units sold and is what the price is counted by, while
    // pieces_consumed is how many pieces left the shelf and is what the cost and
    // the stock movement are counted from. A credit sale moves stock exactly as a
    // cash one does, so it needs both figures just as much.
    item.unitKind = query.value(7).toString();
    // toLongLong, not toInt: pieces_consumed is long long in the struct because it
    // is multiplied by unit_cost_cents in the COGS guard, and reading it back
    // through a 32-bit conversion would truncate the operand that guard checks.
    item.piecesConsumed = query.value(8).toLongLong();
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
                       "reversed_id, unit_kind, pieces_consumed "
                       "FROM customer_transaction_items WHERE customer_transaction_id = ?"));
    query.addBindValue(transactionId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CustomerTransactionItemRepository::findByTransactionId"));
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
                       "(customer_transaction_id, product_id, quantity, unit_price_cents, unit_cost_cents, reversed_id, "
                       "unit_kind, pieces_consumed) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(item.customerTransactionId);
    query.addBindValue(item.productId);
    query.addBindValue(item.quantity);
    query.addBindValue(item.unitPriceCents);
    query.addBindValue(item.unitCostCents);
    query.addBindValue(item.reversedId);
    // Always written rather than left to the column DEFAULT, so a row written
    // here is as well-formed as one written by the migration. A caller that has not
    // been taught about cartons yet still records the pieces that left the shelf;
    // leaving the column to default would record zero, and a zero here is read as
    // goods that cost nothing.
    query.addBindValue(item.unitKind.isEmpty() ? QStringLiteral("piece") : item.unitKind);
    // No fallback: resolveSaleItems is the single source that fills piecesConsumed,
    // and a fallback here would let the stored row disagree with the movement that
    // was already written.
    query.addBindValue(item.piecesConsumed);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CustomerTransactionItemRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

} // namespace app::data