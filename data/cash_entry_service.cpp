#include "cash_entry_service.h"

#include <QDateTime>

#include "core/cash_movement.h"
#include "cash_drawer.h"
#include "cash_movement_repository.h"
#include "audit_log_repository.h"
#include "cash_session_repository.h"
#include "expense_repository.h"
#include "owner_drawing_repository.h"

namespace app::data {

// Writes the audit entry for one of the four operations on this service. Every
// one of them moves money, and all four go through the same transaction as the
// movement itself: an entry that survived a rollback would be a claim the money
// moved when it did not, and an entry written after a failed commit would be the
// same claim with nothing left to catch it.
bool writeAudit(Database& db, const QString& action, int entryId)
{
    AuditLogRepository audit(db);
    return audit.record(action, QStringLiteral("%1 #%2").arg(action).arg(entryId)) != 0;
}


namespace {

bool openSession(Database& db, int cashSessionId, int* outId)
{
    CashSessionRepository sessions(db);
    const auto session = sessions.findById(cashSessionId);
    if (!session.has_value() || session->status != QLatin1String("open")) {
        return false;
    }
    *outId = session->id;
    return true;
}

} // namespace

CashEntryService::CashEntryService(Database& db)
    : m_db(db)
{
}

CashEntryResult CashEntryService::recordExpense(const QString& label, long long amountCents, int cashSessionId)
{
    CashEntryResult result;
    if (amountCents <= 0) {
        result.error = QStringLiteral("expense amount must be positive");
        return result;
    }

    if (!m_db.beginTransaction()) {
        result.error = m_db.lastError();
        return result;
    }

    int sessionId = 0;
    if (!openSession(m_db, cashSessionId, &sessionId)) {
        m_db.rollback();
        result.error = QStringLiteral("cash session is not open");
        return result;
    }

    core::Expense expense;
    expense.createdAt = QDateTime::currentDateTime();
    expense.label = label;
    expense.amountCents = amountCents;
    ExpenseRepository expenses(m_db);
    const int expenseId = expenses.insert(expense);
    if (expenseId == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
    }

    QString movementError;
    const std::optional<int> movementId = cashDrawer::recordMoneyOut(
        m_db, sessionId, core::cashMovementType::kExpense, amountCents,
        label, QStringLiteral("the expense"),
        QStringLiteral("expense"), expenseId, &movementError);
    if (!movementId.has_value()) {
        m_db.rollback();
        result.error = movementError;
        return result;
    }

    if (!writeAudit(m_db, QStringLiteral("expense"), expenseId)) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the entry could not be recorded in the audit log")
            : m_db.lastError();
        return result;
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.entryId = expenseId;
    result.amountCents = amountCents;
    return result;
}

CashEntryResult CashEntryService::recordDrawing(const QString& note, long long amountCents, int cashSessionId)
{
    CashEntryResult result;
    if (amountCents <= 0) {
        result.error = QStringLiteral("drawing amount must be positive");
        return result;
    }

    if (!m_db.beginTransaction()) {
        result.error = m_db.lastError();
        return result;
    }

    int sessionId = 0;
    if (!openSession(m_db, cashSessionId, &sessionId)) {
        m_db.rollback();
        result.error = QStringLiteral("cash session is not open");
        return result;
    }

    core::OwnerDrawing drawing;
    drawing.createdAt = QDateTime::currentDateTime();
    drawing.amountCents = amountCents;
    drawing.note = note;
    OwnerDrawingRepository drawings(m_db);
    const int drawingId = drawings.insert(drawing);
    if (drawingId == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
        return result;
    }

    QString movementError;
    const std::optional<int> movementId = cashDrawer::recordMoneyOut(
        m_db, sessionId, core::cashMovementType::kDrawing, amountCents,
        note, QStringLiteral("the owner drawing"),
        QStringLiteral("owner_drawing"), drawingId, &movementError);
    if (!movementId.has_value()) {
        m_db.rollback();
        result.error = movementError;
        return result;
    }

    if (!writeAudit(m_db, QStringLiteral("owner_drawing"), drawingId)) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the entry could not be recorded in the audit log")
            : m_db.lastError();
        return result;
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.entryId = drawingId;
    result.amountCents = amountCents;
    return result;
}

CashEntryResult CashEntryService::reverseExpense(int expenseId, int cashSessionId)
{
    CashEntryResult result;

    if (!m_db.beginTransaction()) {
        result.error = m_db.lastError();
        return result;
    }

    int sessionId = 0;
    if (!openSession(m_db, cashSessionId, &sessionId)) {
        m_db.rollback();
        result.error = QStringLiteral("cash session is not open");
        return result;
    }

    ExpenseRepository expenses(m_db);
    const auto original = expenses.findById(expenseId);
    if (!original.has_value() || original->amountCents <= 0) {
        m_db.rollback();
        result.error = QStringLiteral("expense is not reversible");
        return result;
    }

    // The mirrored row is written inside this transaction, not one of its own:
    // if it fails, the till must not gain the money, so the whole reversal goes
    // and the caller is told why.
    if (!expenses.reverse(expenseId)) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the expense could not be reversed")
            : m_db.lastError();
        return result;
    }

    QString movementError;
    const std::optional<int> movementId = cashDrawer::recordMoneyIn(
        m_db, sessionId, core::cashMovementType::kRefund, original->amountCents,
        original->label, QStringLiteral("the expense reversal"),
        QStringLiteral("expense"), expenseId, &movementError);
    if (!movementId.has_value()) {
        m_db.rollback();
        result.error = movementError;
        return result;
    }

    if (!writeAudit(m_db, QStringLiteral("entry_reversal"), expenseId)) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the entry could not be recorded in the audit log")
            : m_db.lastError();
        return result;
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.entryId = expenseId;
    result.amountCents = original->amountCents;
    return result;
}

CashEntryResult CashEntryService::reverseDrawing(int drawingId, int cashSessionId)
{
    CashEntryResult result;

    if (!m_db.beginTransaction()) {
        result.error = m_db.lastError();
        return result;
    }

    int sessionId = 0;
    if (!openSession(m_db, cashSessionId, &sessionId)) {
        m_db.rollback();
        result.error = QStringLiteral("cash session is not open");
        return result;
    }

    OwnerDrawingRepository drawings(m_db);
    const auto original = drawings.findById(drawingId);
    if (!original.has_value() || original->amountCents <= 0) {
        m_db.rollback();
        result.error = QStringLiteral("drawing is not reversible");
        return result;
    }

    // Same as the expense above: the mirrored row and the cash movement are
    // one atomic change, and a failed reversal must leave no trace of it.
    if (!drawings.reverse(drawingId)) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the drawing could not be reversed")
            : m_db.lastError();
        return result;
    }

    QString movementError;
    const std::optional<int> movementId = cashDrawer::recordMoneyIn(
        m_db, sessionId, core::cashMovementType::kRefund, original->amountCents,
        original->note, QStringLiteral("the owner drawing reversal"),
        QStringLiteral("owner_drawing"), drawingId, &movementError);
    if (!movementId.has_value()) {
        m_db.rollback();
        result.error = movementError;
        return result;
    }

    if (!writeAudit(m_db, QStringLiteral("entry_reversal"), drawingId)) {
        m_db.rollback();
        result.error = m_db.lastError().isEmpty()
            ? QStringLiteral("the entry could not be recorded in the audit log")
            : m_db.lastError();
        return result;
    }

    if (!m_db.commit()) {
        result.error = m_db.lastError();
        return result;
    }

    result.ok = true;
    result.entryId = drawingId;
    result.amountCents = original->amountCents;
    return result;
}

} // namespace app::data