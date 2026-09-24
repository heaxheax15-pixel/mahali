#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

enum class SyncOpType { Sale, CustomerDebt, CustomerPayment };

struct SyncOperation {
    int opId = 0;
    SyncOpType type = SyncOpType::Sale;
    int entityId = 0;
    long long amountCents = 0;
    QDateTime occurredAt;
    QString note = QStringLiteral("");
    QString deviceId = QStringLiteral("");
};

} // namespace app::core