#pragma once

#include <QString>
#include <QVector>
#include <optional>
#include <vector>

#include "core/customer.h"
#include "database.h"

namespace app::data {

class CustomerRepository {
public:
    explicit CustomerRepository(Database& db);

    std::optional<core::Customer> findById(int id) const;
    std::vector<core::Customer> findByName(const QString& name) const;
    std::vector<core::Customer> findAll() const;
    // Only the customers still being served. What the page lists by default: an
    // account closed out stays in findAll() so its history can still be read.
    QVector<core::Customer> listActive() const;

    int save(const core::Customer& customer);
    bool setActive(int id, bool active);

    // Deletes the customer and the ledger that belongs to them. The lines go
    // first, in one transaction: customer_transactions and payments carry a
    // foreign key onto customers(id) and foreign keys are enforced, so deleting
    // the row on its own is refused by the driver rather than quietly leaving
    // the history orphaned.
    bool remove(int id);

    // What this customer owes right now. One number, because the page, the card
    // and the footer would otherwise each re-derive it and drift apart.
    long long balanceCentsFor(int customerId) const;

private:
    Database& m_db;
};

} // namespace app::data