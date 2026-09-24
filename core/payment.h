#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct Payment {
    int id = 0;
    int customerId = 0;
    long long amountCents = 0;
    QDateTime createdAt;
    QString note = QStringLiteral("");
    int reversedId = 0;
};

} // namespace app::core