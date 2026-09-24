#pragma once

#include <QDate>
#include <QString>
#include <optional>

#include "applied_op_repository.h"
#include "database.h"
#include "payment_repository.h"

namespace app::data {

struct PaymentResult {
    bool ok = false;
    bool alreadyApplied = false;
    int paymentId = 0;
    long long amountCents = 0;
    QString error;
};

// Records a customer payment plus its cash movement in a single transaction,
// and (when a saleId is given) reverses an existing recorded sale.
class PaymentService {
public:
    explicit PaymentService(Database& db);

    PaymentResult recordCustomerPayment(int customerId, long long amountCents, int cashSessionId,
                                        const QString& note,
                                        const core::SyncApplyToken* applyToken = nullptr);

private:
    Database& m_db;
    PaymentRepository m_payments;
    AppliedOpRepository m_appliedOps;
};

} // namespace app::data