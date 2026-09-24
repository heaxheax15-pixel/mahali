#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct OwnerDrawing {
    int id = 0;
    QDateTime createdAt;
    long long amountCents = 0;
    QString note = QStringLiteral("");
    int reversedId = 0;
};

} // namespace app::core