#pragma once

#include <optional>

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
};

} // namespace app::core
