#pragma once

#include <QDate>
#include <QString>
#include <optional>

#include "database.h"
#include "payment_repository.h"

namespace app::data {

struct PaymentResult {
    bool ok = false;
    int paymentId = 0;
    QString error;
};

// Records a customer payment plus its cash movement in a single transaction,
// and (when a saleId is given) reverses an existing recorded sale.
class PaymentService {
public:
    explicit PaymentService(Database& db);

    PaymentResult recordCustomerPayment(int customerId, long long amountCents, int cashSessionId,
                                        const QString& note);

private:
    Database& m_db;
    PaymentRepository m_payments;
};

} // namespace app::data