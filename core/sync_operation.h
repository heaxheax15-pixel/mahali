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
    // "piece", "package" or "weight"; empty means a bare piece line, which is
    // what every line on the wire before this field existed meant.
    QString unitKind = QStringLiteral("");
    // The positive count the stock arithmetic consumes for this line. Zero means
    // "not sent", and the receiving device falls back to its own product data.
    long long piecesConsumed = 0;
};

struct SyncOperation {
    int opId = 0;
    SyncOpType type = SyncOpType::Sale;
    int entityId = 0;
    long long amountCents = 0;
    // Applied on top of the sum of the item lines, the same way a locally
    // recorded sale carries it; zero for every operation before this field.
    long long adjustmentCents = 0;
    QDateTime occurredAt;
    QString note = QStringLiteral("");
    QString deviceId = QStringLiteral("");
    QVector<SyncItem> items;
};

} // namespace app::core
