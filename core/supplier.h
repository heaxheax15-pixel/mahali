#pragma once

#include <QString>

namespace app::core {

struct Supplier {
    int id = 0;
    QString name = QStringLiteral("");
    QString phone = QStringLiteral("");
    QString address = QStringLiteral("");
    QString notes = QStringLiteral("");
    long long openingBalanceCents = 0;
    bool active = true;
};

} // namespace app::core