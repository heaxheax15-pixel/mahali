#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct CashSession {
    int id = 0;
    QDateTime openedAt;
    long long openingFloatCents = 0;
    QDateTime closedAt;
    long long closingCountedCents = 0;
    long long expectedCents = 0;
    long long varianceCents = 0;
    QString status = QStringLiteral(""); // "open" | "closed"
};

} // namespace app::core