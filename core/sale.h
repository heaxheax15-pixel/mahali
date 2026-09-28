#pragma once

#include <QDateTime>
#include <QString>
#include <optional>

namespace app::core {

struct Sale {
    int id = 0;
    QDateTime createdAt;
    long long totalCents = 0;
    QString deviceId = QStringLiteral("");
    bool oversold = false;
    int reversedSaleId = 0;
    // The occasion running when the sale was recorded, absent when the shop had
    // none running. A sale outside every occasion is a normal day, not a gap.
    std::optional<int> occasionId;
};

} // namespace app::core