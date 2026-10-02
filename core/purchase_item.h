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
};

} // namespace app::core