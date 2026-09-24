#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct SupplierTransaction {
    int id = 0;
    int supplierId = 0;
    long long amountCents = 0;
    QDateTime createdAt;
    QString note = QStringLiteral("");
};

} // namespace app::core