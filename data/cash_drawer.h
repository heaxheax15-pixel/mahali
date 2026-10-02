#pragma once

#include <optional>
#include <QString>

#include "core/cash_session.h"
#include "database.h"

namespace app::data {

// How a money-out event books itself against the till.
//
// Every service that takes money out of the drawer — an expense, an owner's
// drawing, a payment to a supplier at the counter — owes the same two things, and
// getting either wrong is what made a day's till reconcile against nothing. So
// they are written once here rather than once per service:
//
//   * the session is the one the caller named, checked to still be open, and
//   * a negative cash_movements row lands in the same transaction.
//
// The session id is never looked up from here on the service's behalf. Finding
// "the open session" would silently book a payment against whichever till
// happened to be running, which is not the one the operator was looking at when
// they pressed save; and it would let a service write to the drawer with no
// session in hand at all, which is exactly the silent write the rules forbid.
namespace cashDrawer {

// The session named by sessionId, when it exists and is still open. Called
// inside the transaction that is about to write, so the answer describes the
// state the write will meet: a till closed by another window between the form
// opening and the payment landing is refused rather than written to.
//
// error is filled in with an operator-readable reason when the answer is no.
std::optional<core::CashSession> stillOpen(Database& db, int sessionId, const QString& what, QString* error);

// Writes the movement for money that physically left the drawer. Runs inside
// the caller's transaction, so the movement and the document that caused it
// commit or roll back together: a payment the drawer never saw must not survive
// on the ledger either.
//
// `what` names the event for the error message ("a payment to a supplier").
// `ref_type` and `ref_id` name the originating document (e.g. "sale", 123) so
// the till can be reconciled line-by-line. A movement without a reference is
// refused: every row in the drawer must trace back to a document the operator
// can name.
std::optional<int> recordMoneyOut(Database& db,
                                  int sessionId,
                                  const QString& type,
                                  long long amountCents,
                                  const QString& note,
                                  const QString& what,
                                  const QString& refType,
                                  int refId,
                                  QString* error);

// The counterpart: money that physically came back into the drawer. Same
// transactional guarantees: if the reversal rolls back, the money-in movement
// rolls back with it, so a drawer that never gained it never claims it did.
// `ref_type` and `ref_id` point at the original movement's document (same as
// the reversal), so the reversal row and its document share the same key.
std::optional<int> recordMoneyIn(Database& db,
                                 int sessionId,
                                 const QString& type,
                                 long long amountCents,
                                 const QString& note,
                                 const QString& what,
                                 const QString& refType,
                                 int refId,
                                 QString* error);

} // namespace cashDrawer

} // namespace app::data