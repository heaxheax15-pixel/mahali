#pragma once

#include <optional>
#include <QString>

namespace app::core {

// One line of a supplier return: how much of which product went back, and what
// it was worth. The product is optional so a return can be recorded for its
// amount alone before the shelves are checked.
struct SupplierReturnItem {
    int id = 0;
    int returnId = 0;
    std::optional<int> productId;
    long long quantity = 0;
    long long unitPriceCents = 0;
    long long totalCents = 0;
    // Whether the quantity above counts pieces or whole cartons, decided per line because a return does not have to match how the goods were bought.
    QString unitKind = QStringLiteral("piece");
    // Counted in long long to match quantity: the two are compared and multiplied
    // together in the COGS overflow guard, and a narrower type would truncate the
    // operand the guard exists to check (LLONG_MIN does not survive an int).
    long long piecesConsumed = 0;
};

} // namespace app::core
