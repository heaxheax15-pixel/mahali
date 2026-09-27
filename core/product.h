#pragma once

#include <QString>

namespace app::core {

struct Product {
    int id = 0;
    // A null barcode means "no barcode" (quick item) and is stored as SQL NULL.
    // An empty-but-not-null barcode is a real, blank barcode and is stored as ''.
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