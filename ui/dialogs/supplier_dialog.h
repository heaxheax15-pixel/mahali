#pragma once

#include <optional>

#include <QWidget>

#include "core/supplier.h"
#include "data/database.h"

namespace app::ui {

// The two dialogs the suppliers page drives. They live here rather than in
// suppliers_page.cpp so the page and anything else that has to name a supplier
// all go through one copy.

// Tab 1 of the supplier card on its own: name, phone, address, the balance as it
// stands, the opening figure and the active flag. Returns nothing when cancelled,
// and a supplier carrying the edited values when accepted. Never saves — the
// caller decides what a returned card means. A new supplier arrives with id = 0.
std::optional<core::Supplier> showSupplierInfoDialog(QWidget* parent, app::data::Database& db,
                                                     const core::Supplier& initial);

// The whole card, opened by supplier id: the info tab plus the purchase invoices,
// the payments, the returns and the merged history. The invoice, payment and
// return entry forms arrive with the next phase; the three buttons say so for
// now rather than doing nothing.
void showSupplierCardDialog(QWidget* parent, app::data::Database& db, int supplierId);

} // namespace app::ui
