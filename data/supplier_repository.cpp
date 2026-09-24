#include "supplier_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

SupplierRepository::SupplierRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::Supplier supplierFromQuery(const QSqlQuery& query)
{
    core::Supplier supplier;
    supplier.id = query.value(0).toInt();
    supplier.name = query.value(1).toString();
    return supplier;
}

} // namespace

std::optional<core::Supplier> SupplierRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, name FROM suppliers WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return supplierFromQuery(query);
}

std::optional<core::Supplier> SupplierRepository::findByName(const QString& name) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, name FROM suppliers WHERE name = ?"));
    query.addBindValue(name);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return supplierFromQuery(query);
}

std::vector<core::Supplier> SupplierRepository::findAll() const
{
    std::vector<core::Supplier> suppliers;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, name FROM suppliers ORDER BY name"));
    if (!query.exec()) {
        return suppliers;
    }
    while (query.next()) {
        suppliers.push_back(supplierFromQuery(query));
    }
    return suppliers;
}

int SupplierRepository::save(const core::Supplier& supplier)
{
    QSqlQuery query(m_db.handle());
    if (supplier.id == 0) {
        query.prepare(QStringLiteral("INSERT INTO suppliers (name) VALUES (?)"));
        query.addBindValue(supplier.name);
        if (!query.exec()) {
            return 0;
        }
        return query.lastInsertId().toInt();
    }
    query.prepare(QStringLiteral("UPDATE suppliers SET name = ? WHERE id = ?"));
    query.addBindValue(supplier.name);
    query.addBindValue(supplier.id);
    if (!query.exec()) {
        return 0;
    }
    return supplier.id;
}

} // namespace app::data