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
    bool reverse(int originalPaymentId, long long amountCents, const QString& note);

    // Whether a reversal row already points at this payment. The same lookup
    // reverse() uses as its guard, exposed so a caller can tell "already
    // refunded" apart from "the driver refused" and say which happened. Joins
    // the caller's transaction, like every other read on this repository.
    bool hasReversalOf(int originalPaymentId) const;

private:
    Database& m_db;
};

} // namespace app::data