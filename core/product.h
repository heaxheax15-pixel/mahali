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
    // Legacy column kept so the existing form and mapper do not have to change at
    // once; the product dialog writes this and pieces_per_package together from
    // the same field. Read pieces_per_package; this one exists for the transition
    // and reads nothing in production.
    int packageSize = 1;
    bool active = true;
    bool soldByWeight = false;
    // What this shop calls a carton. A field and not a constant because the word
    // is printed on the goods and typed by the cashier, so it is theirs to change.
    QString packageName = QStringLiteral("كرتونة");
    // How many pieces one carton holds. The column the stock and COGS arithmetic
    // reads; packageSize above is written beside it by the same field and nothing
    // reads it.
    int piecesPerPackage = 1;
    // The carton's own barcode, computed as the product barcode plus 'c', and
    // empty when the product has no barcode to build one from.
    QString packageBarcode = QStringLiteral("");
    // What one carton costs, in cents, as the supplier invoiced it. Kept as its
    // own figure rather than costPriceCents / piecesPerPackage because that
    // division rounds, and the remainder it drops is what a shelf price has to be
    // checked against — a rounded cost cannot be audited afterwards.
    long long packageCostCents = 0;
};

} // namespace app::core