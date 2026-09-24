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
    sale.deviceId = query.value(3).toString();
    sale.oversold = query.value(4).toInt() != 0;
    sale.reversedSaleId = query.value(5).toInt();
    return sale;
}

const char* kSaleColumns = "id, created_at, total_cents, device_id, oversold, reversed_sale_id";

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
        return sales;
    }
    while (query.next()) {
        sales.push_back(saleFromQuery(query));
    }
    return sales;
}

int SaleRepository::insert(const core::Sale& sale)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO sales (created_at, total_cents, device_id, oversold, reversed_sale_id) "
                       "VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(toIso(sale.createdAt.isValid() ? sale.createdAt : QDateTime::currentDateTime()));
    query.addBindValue(sale.totalCents);
    query.addBindValue(sale.deviceId);
    query.addBindValue(sale.oversold ? 1 : 0);
    query.addBindValue(sale.reversedSaleId);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

} // namespace app::data