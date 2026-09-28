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
    supplier.phone = query.value(2).toString();
    supplier.address = query.value(3).toString();
    supplier.notes = query.value(4).toString();
    supplier.openingBalanceCents = query.value(5).toLongLong();
    supplier.active = query.value(6).toInt() != 0;
    return supplier;
}

} // namespace

std::optional<core::Supplier> SupplierRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "SELECT id, name, phone, address, notes, opening_balance_cents, active "
        "FROM suppliers WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return supplierFromQuery(query);
}

std::optional<core::Supplier> SupplierRepository::findByName(const QString& name) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "SELECT id, name, phone, address, notes, opening_balance_cents, active "
        "FROM suppliers WHERE name = ?"));
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
    query.prepare(QStringLiteral(
        "SELECT id, name, phone, address, notes, opening_balance_cents, active "
        "FROM suppliers ORDER BY name"));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierRepository::findAll"));
        return suppliers;
    }
    while (query.next()) {
        suppliers.push_back(supplierFromQuery(query));
    }
    return suppliers;
}

QVector<core::Supplier> SupplierRepository::listActive() const
{
    QVector<core::Supplier> suppliers;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral(
        "SELECT id, name, phone, address, notes, opening_balance_cents, active "
        "FROM suppliers WHERE active = 1 ORDER BY name"));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierRepository::listActive"));
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
        query.prepare(QStringLiteral(
            "INSERT INTO suppliers (name, phone, address, notes, opening_balance_cents, active) "
            "VALUES (?, ?, ?, ?, ?, ?)"));
        query.addBindValue(supplier.name);
        query.addBindValue(supplier.phone);
        query.addBindValue(supplier.address);
        query.addBindValue(supplier.notes);
        query.addBindValue(supplier.openingBalanceCents);
        query.addBindValue(supplier.active ? 1 : 0);
        if (!query.exec()) {
            m_db.recordError(query.lastError(), QStringLiteral("SupplierRepository::save"));
            return 0;
        }
        return query.lastInsertId().toInt();
    }
    query.prepare(QStringLiteral(
        "UPDATE suppliers SET name = ?, phone = ?, address = ?, notes = ?, "
        "opening_balance_cents = ?, active = ? WHERE id = ?"));
    query.addBindValue(supplier.name);
    query.addBindValue(supplier.phone);
    query.addBindValue(supplier.address);
    query.addBindValue(supplier.notes);
    query.addBindValue(supplier.openingBalanceCents);
    query.addBindValue(supplier.active ? 1 : 0);
    query.addBindValue(supplier.id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierRepository::save"));
        return 0;
    }
    return supplier.id;
}

bool SupplierRepository::setActive(int id, bool active)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE suppliers SET active = ? WHERE id = ?"));
    query.addBindValue(active ? 1 : 0);
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierRepository::setActive"));
        return false;
    }
    return query.numRowsAffected() > 0;
}

long long SupplierRepository::balanceCentsFor(int supplierId) const
{
    // What the supplier is owed: the opening figure, every invoice recorded for
    // them, minus everything paid against it. Invoices count whatever
    // add_to_stock says, because a service that is billed is still a debt even
    // when nothing lands on the shelf. Returns are subtracted in a later phase.
    long long balance = 0;

    QSqlQuery q1(m_db.handle());
    q1.prepare(QStringLiteral(
        "SELECT opening_balance_cents FROM suppliers WHERE id = ?"));
    q1.addBindValue(supplierId);
    if (q1.exec() && q1.next()) {
        balance += q1.value(0).toLongLong();
    }

    QSqlQuery q2(m_db.handle());
    q2.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(total_cents), 0) FROM purchases WHERE supplier_id = ?"));
    q2.addBindValue(supplierId);
    if (q2.exec() && q2.next()) {
        balance += q2.value(0).toLongLong();
    }

    QSqlQuery q3(m_db.handle());
    q3.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(amount_cents), 0) FROM supplier_payments WHERE supplier_id = ?"));
    q3.addBindValue(supplierId);
    if (q3.exec() && q3.next()) {
        balance -= q3.value(0).toLongLong();
    }

    return balance;
}

} // namespace app::data