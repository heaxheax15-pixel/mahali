#pragma once

#include <optional>
#include <QString>

namespace app::core {

struct PurchaseItem {
    int id = 0;
    int purchaseId = 0;
    std::optional<int> productId;
    QString description;
    long long quantity = 0;
    QString unit = QStringLiteral("piece");
    long long unitPriceCents = 0;
    long long totalCents = 0;
    // Links a void item to the original. 0 means "not a void item".
    int reversedId = 0;
    // Whether quantity counts pieces or whole cartons. Purchases take cartons too,
    // not just sales, and a supplier's invoice does not agree with itself about
    // which it wrote — so the unit is recorded per line rather than read off the
    // product.
    QString unitKind = QStringLiteral("piece");
    // Counted in long long to match quantity: the two are compared and multiplied
    // together in the COGS overflow guard, and a narrower type would truncate the
    // operand the guard exists to check (LLONG_MIN does not survive an int).
    long long piecesReceived = 0;
};

} // namespace app::core