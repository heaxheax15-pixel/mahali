#pragma once

namespace app::core {

class ZakatCalculator {
public:
    static long long zakatBaseCents(long long cashSalesCents, long long customerDebtCents);
};

} // namespace app::core