#include "zakat_calculator.h"

namespace app::core {

long long ZakatCalculator::zakatBase(const ZakatInputs& in)
{
    return in.stockValueCents + in.cashCents + in.receivablesCents;
}

long long ZakatCalculator::zakatBaseCents(long long cashSalesCents, long long customerDebtCents)
{
    return cashSalesCents + customerDebtCents;
}

} // namespace app::core