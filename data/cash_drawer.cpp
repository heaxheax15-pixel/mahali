#include "cash_drawer.h"

#include <QDateTime>

#include "cash_movement_repository.h"
#include "cash_session_repository.h"
#include "core/cash_movement.h"
#include "date_utils.h"

namespace app::data::cashDrawer {

std::optional<core::CashSession> stillOpen(Database& db, int sessionId, const QString& what, QString* error)
{
    CashSessionRepository sessions(db);
    const std::optional<core::CashSession> session = sessions.findById(sessionId);
    if (!session.has_value()) {
        if (error) {
            *error = db.lastError().isEmpty()
                ? QStringLiteral("%1 needs an open cash session, and the session it was given (%2) does not "
                                 "exist. Open a session before recording it.")
                      .arg(what)
                      .arg(sessionId)
                : db.lastError();
        }
        return std::nullopt;
    }
    if (session->status != QStringLiteral("open")) {
        if (error) {
            *error = QStringLiteral("%1 needs an open cash session, and session #%2 was closed at %3. "
                                    "Open a new session before recording it.")
                         .arg(what)
                         .arg(sessionId)
                         .arg(session->closedAt.isValid()
                                  ? session->closedAt.toString(QStringLiteral("HH:mm"))
                                  : QStringLiteral("an earlier moment"));
        }
        return std::nullopt;
    }
    return session;
}

std::optional<int> recordMoneyOut(Database& db,
                                   int sessionId,
                                   const QString& type,
                                   long long amountCents,
                                   const QString& note,
                                   const QString& what,
                                   const QString& refType,
                                   int refId,
                                   QString* error)
{
    // Every movement must trace back to a document the operator can name.
    // A movement without a reference is refused: the till must be able to show
    // where every cent came from or went to.
    if (refType.isEmpty() || refId <= 0) {
        if (error) {
            *error = QStringLiteral("%1 requires a reference (type and id)").arg(what);
        }
        return std::nullopt;
    }
    // Written negative, always: the drawer gave the money up. The sign is not
    // derived from `type` here, so a caller cannot pass a positive amount and
    // have the till grow.
    if (amountCents <= 0) {
        if (error) {
            *error = QStringLiteral("%1 must be a positive amount; the drawer movement is its negative")
                         .arg(what);
        }
        return std::nullopt;
    }

    core::CashMovement movement;
    movement.sessionId = sessionId;
    movement.type = type;
    movement.amountCents = -amountCents;
    movement.createdAt = QDateTime::currentDateTime();
    movement.note = note;
    movement.refType = refType;
    movement.refId = refId;

    const int id = CashMovementRepository(db).insert(movement);
    if (id == 0) {
        if (error) {
            *error = db.lastError().isEmpty()
                ? QStringLiteral("the cash movement for %1 could not be written").arg(what)
                : db.lastError();
        }
        return std::nullopt;
    }
    return id;
}

std::optional<int> recordMoneyIn(Database& db,
                                 int sessionId,
                                 const QString& type,
                                 long long amountCents,
                                 const QString& note,
                                 const QString& what,
                                 const QString& refType,
                                 int refId,
                                 QString* error)
{
    if (refType.isEmpty() || refId <= 0) {
        if (error) {
            *error = QStringLiteral("%1 requires a reference (type and id)").arg(what);
        }
        return std::nullopt;
    }
    // Written positive, always: the drawer received the money back. The sign is
    // not derived from `type` here, so a caller cannot pass a negative amount
    // and have the till shrink.
    if (amountCents <= 0) {
        if (error) {
            *error = QStringLiteral("%1 must be a positive amount; the drawer movement is its positive")
                         .arg(what);
        }
        return std::nullopt;
    }

    core::CashMovement movement;
    movement.sessionId = sessionId;
    movement.type = type;
    movement.amountCents = amountCents;
    movement.createdAt = QDateTime::currentDateTime();
    movement.note = note;
    movement.refType = refType;
    movement.refId = refId;

    const int id = CashMovementRepository(db).insert(movement);
    if (id == 0) {
        if (error) {
            *error = db.lastError().isEmpty()
                ? QStringLiteral("the cash movement for %1 could not be written").arg(what)
                : db.lastError();
        }
        return std::nullopt;
    }
    return id;
}

} // namespace app::data::cashDrawer