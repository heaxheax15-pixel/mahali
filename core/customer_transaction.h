#pragma once

#include <QDateTime>
#include <QString>

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
    // A-YYYY-NNNN as printed on the customer's paper. Null until minted, and
    // never set on a reversal row — a reversal is not a new customer
    // transaction, it is a reference to the transaction it undoes, and that
    // transaction still owns the number.
    QString receiptNumber;
};

} // namespace app::core