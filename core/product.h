#pragma once

#include <QString>

namespace app::core {

struct Product {
    int id = 0;
    QString barcode = QStringLiteral("");
    QString name = QStringLiteral("");
    long long costPriceCents = 0;
    long long salePriceCents = 0;
    long long quantity = 0;
    QString unit = QStringLiteral("");
    int packageSize = 1;
    bool active = true;
};

} // namespace app::core