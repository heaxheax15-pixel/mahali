#pragma once

#include <QWidget>

#include "core/purchase.h"
#include "data/database.h"

namespace app::ui {

// What the purchase dialog hands back. saved is false when the operator closed
// the form without recording anything, and purchaseId is the invoice
// PurchaseService wrote — the caller reloads its ledgers with it.
struct PurchaseDialogResult {
    bool saved = false;
    int purchaseId = 0;
};

// Records a supplier invoice: the header, its lines, the stock movements and
// whatever was paid on it, all in one PurchaseService::recordPurchase call so a
// half-written invoice cannot be left behind.
//
// supplierId preselects a supplier in the header; 0 simply leaves the choice to
// the operator. initial prefills the fields and is meant for editing once the
// service grows a way to update an invoice: for now its id is ignored, because
// recordPurchase always inserts, so passing one records a second invoice rather
// than replacing the first.
PurchaseDialogResult showPurchaseDialog(QWidget* parent, app::data::Database& db, int supplierId,
                                        const core::Purchase& initial = {});

} // namespace app::ui
