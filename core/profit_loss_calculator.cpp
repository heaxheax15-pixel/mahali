#include "profit_loss_calculator.h"

namespace app::core {

ProfitLossReport ProfitLossCalculator::compute(long long revenueCents, long long cogsCents, long long expensesCents,
                                               long long drawingsCents)
{
    ProfitLossReport report;
    report.revenueCents = revenueCents;
    report.cogsCents = cogsCents;
    report.grossProfitCents = revenueCents - cogsCents;
    report.expensesCents = expensesCents;
    report.netProfitCents = report.grossProfitCents - expensesCents;
    report.drawingsCents = drawingsCents;
    return report;
}

} // namespace app::core