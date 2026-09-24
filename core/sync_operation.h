#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace app::core {

enum class SyncOpType { Sale, CustomerDebt, CustomerPayment };

struct SyncItem {
    int productId = 0;
    long long quantity = 0;
    long long unitPriceCents = 0;
};

struct SyncOperation {
    int opId = 0;
    SyncOpType type = SyncOpType::Sale;
    int entityId = 0;
    long long amountCents = 0;
    QDateTime occurredAt;
    QString note = QStringLiteral("");
    QString deviceId = QStringLiteral("");
    QVector<SyncItem> items;
};

} // namespace app::core
