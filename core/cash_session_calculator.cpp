#include "cash_session_calculator.h"

namespace app::core {

long long CashSessionCalculator::expectedTotalCents(long long openingFloatCents, long long movementsSumCents)
{
    return openingFloatCents + movementsSumCents;
}

long long CashSessionCalculator::varianceCents(long long closingCountedCents, long long expectedTotalCents)
{
    return closingCountedCents - expectedTotalCents;
}

} // namespace app::core