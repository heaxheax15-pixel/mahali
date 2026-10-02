#include "payment_service.h"

#include <QDateTime>

#include "core/cash_movement.h"
#include "cash_drawer.h"
#include "cash_movement_repository.h"
#include "cash_session_repository.h"
#include "audit_log_repository.h"

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

    QString movementError;
    const std::optional<int> movementId = cashDrawer::recordMoneyIn(
        m_db, session->id, core::cashMovementType::kCustomerPayment, amountCents,
        note, QStringLiteral("the customer payment"),
        QStringLiteral("payment"), paymentId, &movementError);
    if (!movementId.has_value()) {
        m_db.rollback();
        result.error = movementError;
        return result;
    }

    // Inside the transaction, and fatal if it fails. An entry naming this payment
    // that gets rolled back would be a claim the drawer never gained the money;
    // an entry written after the commit would be a claim that survives a commit
    // that failed.
    AuditLogRepository audit(m_db);
    const QString auditTarget = QStringLiteral("payment %1, customer %2").arg(paymentId).arg(customerId);
    const int auditId = applyToken
        ? audit.recordAs(applyToken->deviceId, QStringLiteral("customer_payment"), auditTarget)
        : audit.record(QStringLiteral("customer_payment"), auditTarget);
    if (auditId
        == 0) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the payment could not be recorded in the audit log")
            : m_db.lastError();
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

PaymentResult PaymentService::refundCustomerPayment(int paymentId, int cashSessionId, const QString& note)
{
    PaymentResult result;
    const auto original = m_payments.findById(paymentId);
    if (!original.has_value() || original->amountCents <= 0 || original->reversedId != 0) {
        result.error = QStringLiteral("payment is not refundable");
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

    // The mirrored row is written inside this transaction, not one of its own:
    // if it fails, the till must not gain the money, so the whole refund goes
    // and the caller is told why.
    if (!m_payments.reverse(paymentId, original->amountCents, note)) {
        // PaymentRepository::reverse reports its own reason through Database when
        // the driver refused the insert. It stays silent in the one case it can
        // still detect on its own: a refund a previous refund already accounted
        // for. Saying so plainly matters here — this is the cashier's second
        // click on the same row, and "the payment could not be reversed" would
        // read as a fault on the till rather than a refund that is done.
        //
        // Read before the rollback, while the guard's answer is still the one
        // this call got. The first refund committed on its own, so the row is
        // there either way; asking first keeps the two answers about the same
        // state.
        const bool alreadyRefunded = m_payments.hasReversalOf(paymentId);
        m_db.rollback();
        if (alreadyRefunded) {
            result.error = QStringLiteral("this payment has already been refunded");
        } else {
            result.error = m_db.lastError().isEmpty()
                ? QStringLiteral("the payment could not be reversed")
                : m_db.lastError();
        }
        return result;
    }

    QString movementError;
    const std::optional<int> movementId = cashDrawer::recordMoneyOut(
        m_db, session->id, core::cashMovementType::kRefund, original->amountCents,
        note, QStringLiteral("the customer payment refund"),
        QStringLiteral("payment"), paymentId, &movementError);
    if (!movementId.has_value()) {
        m_db.rollback();
        result.error = movementError;
        return result;
    }

    AuditLogRepository audit(m_db);
    if (audit.record(QStringLiteral("customer_payment_refund"), QStringLiteral("payment %1").arg(paymentId)) == 0) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the refund could not be recorded in the audit log")
            : m_db.lastError();
        return result;
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.amountCents = -original->amountCents;
    return result;
}

} // namespace app::data
