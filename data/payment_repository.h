#pragma once

#include <QDateTime>
#include <optional>
#include <vector>

#include "core/payment.h"
#include "database.h"

namespace app::data {

class PaymentRepository {
public:
    explicit PaymentRepository(Database& db);

    std::optional<core::Payment> findById(int id) const;
    std::vector<core::Payment> findByCustomerId(int customerId) const;
    std::vector<core::Payment> findBetween(const QDateTime& from, const QDateTime& to) const;

    int insert(const core::Payment& payment);
    void reverse(int originalPaymentId, long long amountCents, const QString& note);

private:
    Database& m_db;
};

} // namespace app::data