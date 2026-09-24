#pragma once

namespace app::core {

struct SaleItem {
    int id = 0;
    int saleId = 0;
    int productId = 0;
    long long quantity = 0;
    long long unitPriceCents = 0;
    long long unitCostCents = 0;
    int reversedId = 0;
};

} // namespace app::core