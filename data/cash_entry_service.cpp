#include "cash_entry_service.h"

#include <QDateTime>

#include "cash_movement_repository.h"
#include "cash_session_repository.h"
#include "expense_repository.h"
#include "owner_drawing_repository.h"

namespace app::data {

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

    core::CashMovement movement;
    movement.sessionId = sessionId;
    movement.type = QStringLiteral("expense");
    movement.amountCents = -amountCents;
    movement.createdAt = expense.createdAt;
    movement.note = label;
    if (CashMovementRepository(m_db).insert(movement) == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
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

    core::CashMovement movement;
    movement.sessionId = sessionId;
    movement.type = QStringLiteral("drawing");
    movement.amountCents = -amountCents;
    movement.createdAt = drawing.createdAt;
    movement.note = note;
    if (CashMovementRepository(m_db).insert(movement) == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
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

    expenses.reverse(expenseId);

    core::CashMovement movement;
    movement.sessionId = sessionId;
    movement.type = QStringLiteral("refund");
    movement.amountCents = original->amountCents;
    movement.createdAt = QDateTime::currentDateTime();
    movement.note = original->label;
    if (CashMovementRepository(m_db).insert(movement) == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
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

    drawings.reverse(drawingId);

    core::CashMovement movement;
    movement.sessionId = sessionId;
    movement.type = QStringLiteral("refund");
    movement.amountCents = original->amountCents;
    movement.createdAt = QDateTime::currentDateTime();
    movement.note = original->note;
    if (CashMovementRepository(m_db).insert(movement) == 0) {
        m_db.rollback();
        result.error = m_db.lastError();
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