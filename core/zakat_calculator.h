#pragma once

namespace app::core {

struct ZakatInputs {
    long long stockValueCents = 0;
    long long cashCents = 0;
    long long receivablesCents = 0;
};

class ZakatCalculator {
public:
    static long long zakatBase(const ZakatInputs& in);

    static long long zakatBaseCents(long long cashSalesCents, long long customerDebtCents);
};

} // namespace app::core