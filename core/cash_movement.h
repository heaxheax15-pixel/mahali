#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

// The kinds of movement the drawer can be involved in. cash_movements.type is
// free text and reports group by it, so the set is named here rather than spelled
// out at each call site: a type written by one service and not recognised by a
// report's label function shows up as a raw word in the operator's day, which is
// how "supplier_payment" went missing from the till list in the first place.
namespace cashMovementType {

inline const QString kSale = QStringLiteral("sale");
inline const QString kCustomerPayment = QStringLiteral("customer_payment");
inline const QString kExpense = QStringLiteral("expense");
inline const QString kDrawing = QStringLiteral("drawing");
inline const QString kSupplierPayment = QStringLiteral("supplier_payment");
inline const QString kSupplierPaymentReversal = QStringLiteral("supplier_payment_reversal");
inline const QString kRefund = QStringLiteral("refund");

} // namespace cashMovementType

// Money that physically left the drawer, and so is written as a negative amount:
// an expense, an owner's drawing, a payment to a supplier taken at the counter.
// Money that came in is positive. The sign is the whole record — nothing
// derives it from the type, so a type is never stored with the wrong sign.
inline bool isMoneyOutgoing(const QString& type)
{
    return type == cashMovementType::kExpense || type == cashMovementType::kDrawing
        || type == cashMovementType::kSupplierPayment || type == cashMovementType::kRefund;
}

struct CashMovement {
    int id = 0;
    int sessionId = 0;
    QString type = QStringLiteral(""); // one of cashMovementType:: above
    long long amountCents = 0;
    QDateTime createdAt;
    QString note = QStringLiteral("");
    // Reference to the originating document. Old rows (before this column)
    // have NULL in both fields and are counted in the "unreferenced" bucket.
    QString refType = QStringLiteral("");
    int refId = 0;
};

} // namespace app::core