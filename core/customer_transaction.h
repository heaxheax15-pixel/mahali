#pragma once

#include <QDateTime>

namespace app::core {

struct CustomerTransaction {
    int id = 0;
    int customerId = 0;
    long long amountCents = 0;
    // The same hand-typed change the till can carry, written here too: a credit
    // sale is an invoice like any other, so a customer can be charged or credited
    // a round figure that no line item explains. amount_cents is the sum of the
    // transaction's items plus this.
    long long adjustmentCents = 0;
    QDateTime createdAt;
    int reversedTransactionId = 0;
};

} // namespace app::core