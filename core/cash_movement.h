#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct CashMovement {
    int id = 0;
    int sessionId = 0;
    QString type = QStringLiteral(""); // e.g. "sale" | "customer_payment" | "expense" | "drawing" | "refund"
    long long amountCents = 0;
    QDateTime createdAt;
    QString note = QStringLiteral("");
};

} // namespace app::core