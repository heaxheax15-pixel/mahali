#pragma once

#include <QString>

namespace app::core {

struct Product {
    int id = 0;
    // A barcode that is empty, whether null or blank, means "no barcode" (a quick
    // item) and is stored as SQL NULL. That is what keeps products.barcode, which
    // is UNIQUE, from refusing every product saved without one.
    QString barcode = QStringLiteral("");
    QString name = QStringLiteral("");
    long long costPriceCents = 0;
    long long salePriceCents = 0;
    long long quantity = 0;
    QString unit = QStringLiteral("");
    int packageSize = 1;
    bool active = true;
    bool soldByWeight = false;
};

} // namespace app::core