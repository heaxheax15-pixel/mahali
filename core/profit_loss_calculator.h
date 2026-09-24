#pragma once

namespace app::core {

struct ProfitLossReport {
    long long revenueCents = 0;
    long long cogsCents = 0;
    long long grossProfitCents = 0;
    long long expensesCents = 0;
    long long netProfitCents = 0;
    long long drawingsCents = 0;
};

class ProfitLossCalculator {
public:
    static ProfitLossReport compute(long long revenueCents, long long cogsCents, long long expensesCents,
                                    long long drawingsCents);
};

} // namespace app::core