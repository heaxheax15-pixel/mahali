#pragma once

#include <QDateTime>

namespace app::core {

struct CustomerTransaction {
    int id = 0;
    int customerId = 0;
    long long amountCents = 0;
    QDateTime createdAt;
    int reversedTransactionId = 0;
};

} // namespace app::core