#pragma once

#include <QWidget>

#include "data/database.h"

namespace app::ui {

// What the return dialog hands back. saved is false when the operator closed the
// form without recording anything, and returnId is the return
// SupplierReturnService wrote — the caller reloads its ledgers with it.
struct SupplierReturnDialogResult {
    bool saved = false;
    int returnId = 0;
};

// Records goods handed back to a supplier, in one of the two shapes the service
// accepts: credited against one of their invoices, line by line, or credited for
// its amount alone for goods that never appeared on a purchase.
//
// Note what the form does not offer: a "take the goods off the shelves" choice.
// SupplierReturnService decides that itself — a linked return always leaves the
// shelves, a general one never does — so there is nothing here that would change
// it, and offering the choice would be a control that does nothing.
SupplierReturnDialogResult showSupplierReturnDialog(QWidget* parent, app::data::Database& db,
                                                    int supplierId);

} // namespace app::ui
