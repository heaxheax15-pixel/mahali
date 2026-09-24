#include "zakat_calculator.h"

namespace app::core {

long long ZakatCalculator::zakatBaseCents(long long cashSalesCents, long long customerDebtCents)
{
    return cashSalesCents + customerDebtCents;
}

} // namespace app::core