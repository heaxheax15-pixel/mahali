#pragma once

#include <QString>

namespace app::core {

struct SaleItem {
    int id = 0;
    int saleId = 0;
    int productId = 0;
    long long quantity = 0;
    long long unitPriceCents = 0;
    long long unitCostCents = 0;
    int reversedId = 0;
    // Whether quantity above counts pieces or whole cartons. Kept beside quantity
    // rather than folded into it: 3 pieces and 3 cartons are the same number and
    // take a different number off the shelf, so nothing left on the row could tell
    // them apart if the unit were not recorded.
    QString unitKind = QStringLiteral("piece");
    // Counted in long long to match quantity: the two are compared and multiplied
    // together in the COGS overflow guard, and a narrower type would truncate the
    // operand the guard exists to check (LLONG_MIN does not survive an int).
    long long piecesConsumed = 0;
};

} // namespace app::core