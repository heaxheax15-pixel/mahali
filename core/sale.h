#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct Sale {
    int id = 0;
    QDateTime createdAt;
    long long totalCents = 0;
    QString deviceId = QStringLiteral("");
    bool oversold = false;
    int reversedSaleId = 0;
};

} // namespace app::core