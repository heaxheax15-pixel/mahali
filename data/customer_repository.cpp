#include "customer_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

CustomerRepository::CustomerRepository(Database& db)
    : m_db(db)
{
}

namespace {

// The column list every read shares, so a column added here cannot be forgotten
// in one query and quietly default in another.
const char* kCustomerColumns = "id, name, phone, opening_balance_cents, active";

core::Customer customerFromQuery(const QSqlQuery& query)
{
    core::Customer customer;
    customer.id = query.value(0).toInt();
    customer.name = query.value(1).toString();
    customer.phone = query.value(2).toString();
    customer.openingBalanceCents = query.value(3).toLongLong();
    customer.active = query.value(4).toInt() != 0;
    return customer;
}

} // namespace

std::optional<core::Customer> CustomerRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM customers WHERE id = ?")
                      .arg(QLatin1StringView(kCustomerColumns)));
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
    query.prepare(QStringLiteral("SELECT %1 FROM customers WHERE name = ?")
                      .arg(QLatin1StringView(kCustomerColumns)));
    query.addBindValue(name);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CustomerRepository::findByName"));
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
    query.prepare(QStringLiteral("SELECT %1 FROM customers ORDER BY name")
                      .arg(QLatin1StringView(kCustomerColumns)));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CustomerRepository::findAll"));
        return customers;
    }
    while (query.next()) {
        customers.push_back(customerFromQuery(query));
    }
    return customers;
}

QVector<core::Customer> CustomerRepository::listActive() const
{
    QVector<core::Customer> customers;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM customers WHERE active = 1 ORDER BY name")
                      .arg(QLatin1StringView(kCustomerColumns)));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CustomerRepository::listActive"));
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
        query.prepare(QStringLiteral(
            "INSERT INTO customers (name, phone, opening_balance_cents, active) VALUES (?, ?, ?, ?)"));
        query.addBindValue(customer.name);
        query.addBindValue(customer.phone);
        query.addBindValue(customer.openingBalanceCents);
        query.addBindValue(customer.active ? 1 : 0);
        if (!query.exec()) {
            m_db.recordError(query.lastError(), QStringLiteral("CustomerRepository::save"));
            return 0;
        }
        return query.lastInsertId().toInt();
    }

    query.prepare(QStringLiteral(
        "UPDATE customers SET name = ?, phone = ?, opening_balance_cents = ?, active = ? "
        "WHERE id = ?"));
    query.addBindValue(customer.name);
    query.addBindValue(customer.phone);
    query.addBindValue(customer.openingBalanceCents);
    query.addBindValue(customer.active ? 1 : 0);
    query.addBindValue(customer.id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CustomerRepository::save"));
        return 0;
    }
    return customer.id;
}

bool CustomerRepository::setActive(int id, bool active)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE customers SET active = ? WHERE id = ?"));
    query.addBindValue(active ? 1 : 0);
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("CustomerRepository::setActive"));
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool CustomerRepository::remove(int id)
{
    if (!m_db.beginTransaction()) {
        m_db.recordError(QSqlError(QStringLiteral(""), QStringLiteral("cannot begin transaction")),
                         QStringLiteral("CustomerRepository::remove"));
        return false;
    }

    // Children before parent, and the item lines before the transactions they
    // hang off. Any failure rolls the whole thing back, so a customer is never
    // left without the history that explains their balance.
    const QStringList statements = {
        QStringLiteral("DELETE FROM customer_transaction_items WHERE customer_transaction_id IN "
                       "(SELECT id FROM customer_transactions WHERE customer_id = ?)"),
        QStringLiteral("DELETE FROM customer_transactions WHERE customer_id = ?"),
        QStringLiteral("DELETE FROM payments WHERE customer_id = ?"),
        QStringLiteral("DELETE FROM customers WHERE id = ?"),
    };
    for (const QString& statement : statements) {
        QSqlQuery query(m_db.handle());
        query.prepare(statement);
        query.addBindValue(id);
        if (!query.exec()) {
            m_db.recordError(query.lastError(), QStringLiteral("CustomerRepository::remove"));
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return false;
    }
    return true;
}

long long CustomerRepository::balanceCentsFor(int customerId) const
{
    // What the customer owes: the figure they started on, plus everything put on
    // their ledger, minus everything taken off it.
    //
    // customer_transactions is summed with no filter on the sign. That is
    // deliberate: a reversal is written as a negative row against the original
    // (the table carries reversed_transaction_id precisely to mark it), so
    // filtering to amount_cents > 0 would drop every reversal and leave a
    // cancelled credit sale still showing as debt. Payments are subtracted the
    // same way for the same reason — a refunded payment is a negative row.
    long long balance = 0;

    QSqlQuery opening(m_db.handle());
    opening.prepare(QStringLiteral("SELECT opening_balance_cents FROM customers WHERE id = ?"));
    opening.addBindValue(customerId);
    if (opening.exec() && opening.next()) {
        balance += opening.value(0).toLongLong();
    }

    QSqlQuery transactions(m_db.handle());
    transactions.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(amount_cents), 0) FROM customer_transactions WHERE customer_id = ?"));
    transactions.addBindValue(customerId);
    if (transactions.exec() && transactions.next()) {
        balance += transactions.value(0).toLongLong();
    }

    QSqlQuery payments(m_db.handle());
    payments.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(amount_cents), 0) FROM payments WHERE customer_id = ?"));
    payments.addBindValue(customerId);
    if (payments.exec() && payments.next()) {
        balance -= payments.value(0).toLongLong();
    }

    return balance;
}

} // namespace app::data
