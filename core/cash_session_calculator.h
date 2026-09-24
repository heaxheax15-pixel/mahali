#pragma once

namespace app::core {

class CashSessionCalculator {
public:
    static long long expectedTotalCents(long long openingFloatCents, long long movementsSumCents);
    static long long varianceCents(long long closingCountedCents, long long expectedTotalCents);
};

} // namespace app::core