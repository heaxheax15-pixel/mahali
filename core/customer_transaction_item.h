#pragma once

#include <QString>

namespace app::core {

struct CustomerTransactionItem {
    int id = 0;
    int customerTransactionId = 0;
    int productId = 0;
    long long quantity = 0;
    long long unitPriceCents = 0;
    long long unitCostCents = 0;
    int reversedId = 0;
    // Pieces or cartons, as on a cash sale line. A credit sale moves stock exactly
    // as a cash one does, so a carton line on a tab that could not say how many
    // pieces it took would reconcile the shelf against the till wrongly — the
    // goods leave the shelf either way.
    QString unitKind = QStringLiteral("piece");
    // Counted in long long to match quantity: the two are compared and multiplied
    // together in the COGS overflow guard, and a narrower type would truncate the
    // operand the guard exists to check (LLONG_MIN does not survive an int).
    long long piecesConsumed = 0;
};

} // namespace app::core