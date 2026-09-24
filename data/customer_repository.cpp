#include "customer_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

CustomerRepository::CustomerRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::Customer customerFromQuery(const QSqlQuery& query)
{
    core::Customer customer;
    customer.id = query.value(0).toInt();
    customer.name = query.value(1).toString();
    customer.phone = query.value(2).toString();
    return customer;
}

} // namespace

std::optional<core::Customer> CustomerRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, name, phone FROM customers WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return customerFromQuery(query);
}

std::vector<core::Customer> CustomerRepository::findByName(const QString& name) const
{
    std::vector<core::Customer> customers;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, name, phone FROM customers WHERE name = ?"));
    query.addBindValue(name);
    if (!query.exec()) {
        return customers;
    }
    while (query.next()) {
        customers.push_back(customerFromQuery(query));
    }
    return customers;
}

std::vector<core::Customer> CustomerRepository::findAll() const
{
    std::vector<core::Customer> customers;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, name, phone FROM customers ORDER BY name"));
    if (!query.exec()) {
        return customers;
    }
    while (query.next()) {
        customers.push_back(customerFromQuery(query));
    }
    return customers;
}

int CustomerRepository::save(const core::Customer& customer)
{
    QSqlQuery query(m_db.handle());
    if (customer.id == 0) {
        query.prepare(QStringLiteral("INSERT INTO customers (name, phone) VALUES (?, ?)"));
        query.addBindValue(customer.name);
        query.addBindValue(customer.phone);
        if (!query.exec()) {
            return 0;
        }
        return query.lastInsertId().toInt();
    }

    query.prepare(QStringLiteral("UPDATE customers SET name = ?, phone = ? WHERE id = ?"));
    query.addBindValue(customer.name);
    query.addBindValue(customer.phone);
    query.addBindValue(customer.id);
    if (!query.exec()) {
        return 0;
    }
    return customer.id;
}

} // namespace app::data