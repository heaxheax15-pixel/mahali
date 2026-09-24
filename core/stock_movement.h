#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct StockMovement {
    int id = 0;
    int productId = 0;
    long long delta = 0;
    QString reason = QStringLiteral("");
    QDateTime createdAt;
    int reversedId = 0;
};

} // namespace app::core