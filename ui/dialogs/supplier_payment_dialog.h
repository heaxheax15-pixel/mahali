#pragma once

#include <QWidget>

#include <optional>

#include "data/database.h"

namespace app::ui {

// What the payment dialog hands back. saved is false when the operator closed
// the form without recording anything, and paymentId is the payment
// SupplierPaymentService wrote — the caller reloads its ledgers with it.
struct SupplierPaymentDialogResult {
    bool saved = false;
    int paymentId = 0;
};

// Records what was paid to a supplier, either against one of their open
// invoices or as a general payment taken off the balance as a whole.
//
// purchaseId preselects an invoice in the list; it is honoured only when that
// invoice is one of the open ones, since a payment against a settled invoice
// would be an advance the invoice list has no way to show.
SupplierPaymentDialogResult showSupplierPaymentDialog(QWidget* parent, app::data::Database& db,
                                                      int supplierId,
                                                      std::optional<int> purchaseId = std::nullopt);

} // namespace app::ui
