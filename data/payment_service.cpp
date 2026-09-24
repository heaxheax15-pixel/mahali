#include "payment_service.h"

#include <QDateTime>

#include "cash_movement_repository.h"
#include "cash_session_repository.h"

namespace app::data {

PaymentService::PaymentService(Database& db)
    : m_db(db)
    , m_payments(db)
{
}

PaymentResult PaymentService::recordCustomerPayment(int customerId, long long amountCents, int cashSessionId,
                                                   const QString& note)
{
    PaymentResult result;
    if (amountCents <= 0) {
        result.error = QStringLiteral("payment amount must be positive");
        return result;
    }
    if (customerId <= 0) {
        result.error = QStringLiteral("customer id is invalid");
        return result;
    }

    if (!m_db.beginTransaction()) {
        result.error = m_db.lastError();
        return result;
    }

    CashSessionRepository cashSessions(m_db);
    const auto session = cashSessions.findById(cashSessionId);
    if (!session.has_value() || session->status != QStringLiteral("open")) {
        m_db.rollback();
        result.error = QStringLiteral("cash session is not open");
        return result;
    }

    core::Payment payment;
    payment.customerId = customerId;
    payment.amountCents = amountCents;
    payment.createdAt = QDateTime::currentDateTime();
    payment.note = note;
    const int paymentId = m_payments.insert(payment);
    if (paymentId == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
    }

    core::CashMovement movement;
    movement.sessionId = session->id;
    movement.type = QStringLiteral("customer_payment");
    movement.amountCents = amountCents;
    movement.createdAt = payment.createdAt;
    movement.note = note;
    CashMovementRepository cashMovements(m_db);
    if (cashMovements.insert(movement) == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.paymentId = paymentId;
    return result;
}

} // namespace app::data
