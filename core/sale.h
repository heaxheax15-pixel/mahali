#pragma once

#include <QDateTime>
#include <QString>
#include <optional>

namespace app::core {

struct Sale {
    int id = 0;
    QDateTime createdAt;
    long long totalCents = 0;
    // What the cashier changed by hand on the whole invoice: a surcharge or a
    // discount, already signed. Kept beside the total rather than folded into it,
    // because total_cents is what the till and the reports add up, and a figure
    // that has been edited without leaving a trace of what was edited is a figure
    // nobody can check afterwards. The invariant: total_cents is always the sum of
    // the sale's items plus this, and it holds for a reversal too.
    long long adjustmentCents = 0;
    QString deviceId = QStringLiteral("");
    bool oversold = false;
    int reversedSaleId = 0;
    // The occasion running when the sale was recorded, absent when the shop had
    // none running. A sale outside every occasion is a normal day, not a gap.
    std::optional<int> occasionId;
};

} // namespace app::core