#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct StockMovement {
    int id = 0;
    int productId = 0;
    long long delta = 0;
    QString reason = QStringLiteral("");
    // What produced the movement, e.g. "Purchase #42". Empty when the movement
    // stands on its own, such as a manual stock correction.
    QString reference = QStringLiteral("");
    QDateTime createdAt;
    int reversedId = 0;
};

} // namespace app::core