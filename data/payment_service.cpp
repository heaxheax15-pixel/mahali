#include "payment_service.h"

#include <QDateTime>

#include "cash_movement_repository.h"
#include "cash_session_repository.h"

namespace app::data {

PaymentService::PaymentService(Database& db)
    : m_db(db)
    , m_payments(db)
    , m_appliedOps(db)
{
}

PaymentResult PaymentService::recordCustomerPayment(int customerId, long long amountCents, int cashSessionId,
                                                    const QString& note,
                                                    const core::SyncApplyToken* applyToken)
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

    if (applyToken) {
        if (const auto existing = m_appliedOps.findByDeviceOp(applyToken->deviceId, applyToken->opId)) {
            result.ok = true;
            result.alreadyApplied = true;
            result.paymentId = existing->entityId;
            result.amountCents = existing->totalCents;
            return result;
        }
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

    if (applyToken) {
        core::AppliedOpRecord record;
        record.opId = applyToken->opId;
        record.deviceId = applyToken->deviceId;
        record.opType = static_cast<int>(core::SyncOpType::CustomerPayment);
        record.entityId = paymentId;
        record.totalCents = amountCents;
        record.appliedAt = QDateTime::currentDateTime();
        if (m_appliedOps.insert(record) == 0) {
            m_db.rollback();
            const auto existing = m_appliedOps.findByDeviceOp(applyToken->deviceId, applyToken->opId);
            if (existing.has_value()) {
                result.ok = true;
                result.alreadyApplied = true;
                result.paymentId = existing->entityId;
                result.amountCents = existing->totalCents;
                return result;
            }
            result.error = m_db.lastError();
            return result;
        }
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.paymentId = paymentId;
    result.amountCents = amountCents;
    return result;
}

} // namespace app::data
