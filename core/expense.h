#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct Expense {
    int id = 0;
    QDateTime createdAt;
    QString label = QStringLiteral("");
    long long amountCents = 0;
    int reversedId = 0;
};

} // namespace app::core