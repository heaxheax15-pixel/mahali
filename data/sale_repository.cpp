#include "sale_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

SaleRepository::SaleRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::Sale saleFromQuery(const QSqlQuery& query)
{
    core::Sale sale;
    sale.id = query.value(0).toInt();
    sale.createdAt = fromIso(query.value(1).toString()).value_or(QDateTime());
    sale.totalCents = query.value(2).toLongLong();
    sale.adjustmentCents = query.value(7).toLongLong();
    sale.deviceId = query.value(3).toString();
    sale.oversold = query.value(4).toInt() != 0;
    sale.reversedSaleId = query.value(5).toInt();
    // occasion_id is nullable and read as a QVariant, so the null has to stay
    // distinct from an occasion numbered zero.
    if (const std::optional<long long> occasionId = toLongLong(query.value(6))) {
        sale.occasionId = static_cast<int>(*occasionId);
    }
    sale.receiptNumber = query.value(8).toString();
    return sale;
}

// adjustment_cents comes last so the indices above keep their meaning: the column
// was added to the table after the rest of it, and every query in this file reads
// by position. receipt_number follows it for the same reason, and customer_id
// after that is selected but read by nothing yet: nothing writes it in this
// phase, so it is NULL on every row and a reader that ignored the column would
// see exactly the same sales.
const char* kSaleColumns =
    "id, created_at, total_cents, device_id, oversold, reversed_sale_id, occasion_id, adjustment_cents, "
    "receipt_number, customer_id";

} // namespace

std::optional<core::Sale> SaleRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM sales WHERE id = ?").arg(QLatin1StringView(kSaleColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return saleFromQuery(query);
}

std::vector<core::Sale> SaleRepository::findBetween(const QDateTime& from, const QDateTime& to) const
{
    std::vector<core::Sale> sales;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM sales WHERE created_at >= ? AND created_at <= ? ORDER BY created_at")
                      .arg(QLatin1StringView(kSaleColumns)));
    query.addBindValue(toIso(from));
    query.addBindValue(toIso(to));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SaleRepository::findBetween"));
        return sales;
    }
    while (query.next()) {
        sales.push_back(saleFromQuery(query));
    }
    return sales;
}

std::vector<core::Sale> SaleRepository::findAll() const
{
    std::vector<core::Sale> sales;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM sales ORDER BY created_at").arg(QLatin1StringView(kSaleColumns)));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SaleRepository::findAll"));
        return sales;
    }
    while (query.next()) {
        sales.push_back(saleFromQuery(query));
    }
    return sales;
}

int SaleRepository::countByDeviceId(const QString& deviceId) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM sales WHERE device_id = ?"));
    query.addBindValue(deviceId);
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

int SaleRepository::insert(const core::Sale& sale)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO sales (created_at, total_cents, device_id, oversold, reversed_sale_id, occasion_id, adjustment_cents, receipt_number) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(toIso(sale.createdAt.isValid() ? sale.createdAt : QDateTime::currentDateTime()));
    query.addBindValue(sale.totalCents);
    query.addBindValue(sale.deviceId);
    query.addBindValue(sale.oversold ? 1 : 0);
    query.addBindValue(sale.reversedSaleId);
    // QVariant() binds a real NULL, which is what an unstamped sale needs: the
    // column is nullable, unlike the neighbours that carry a 0 sentinel.
    if (sale.occasionId.has_value()) {
        query.addBindValue(*sale.occasionId);
    } else {
        query.addBindValue(QVariant());
    }
    query.addBindValue(sale.adjustmentCents);
    // An empty number binds a real NULL rather than "". A row that has not been
    // through the numbering service holds no number at all, and the distinction
    // has to survive storage: `WHERE receipt_number IS NULL` is what finds the
    // unreceipted rows, a query that can never be written against "".
    if (sale.receiptNumber.isEmpty()) {
        query.addBindValue(QVariant());
    } else {
        query.addBindValue(sale.receiptNumber);
    }
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SaleRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

} // namespace app::data