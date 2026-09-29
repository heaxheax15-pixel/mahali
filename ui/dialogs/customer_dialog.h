#pragma once

#include <optional>

#include <QVector>
#include <QWidget>

#include "core/customer.h"
#include "data/database.h"

class QDialog;

namespace app::core {
struct SaleItem;
} // namespace app::core

namespace app::ui {

// The three dialogs the customers page drives. They live here rather than in
// customers_page.cpp so the customer card, the page and anything else that needs
// to record a debt or a payment all go through one copy.

// Tab 1 of the customer card on its own: name, phone, the balance as it stands,
// the opening figure, and the active flag. Returns nothing when cancelled, and
// a customer carrying the edited values when accepted. Never saves — the caller
// decides what a returned card means.
std::optional<core::Customer> showCustomerInfoDialog(QWidget* parent, app::data::Database& db,
                                                     const core::Customer& initial);

// Picks the products for a credit sale and records them against the customer.
// True when a sale was written.
bool showNewDebtDialog(QWidget* parent, app::data::Database& db, int customerId);

// Asks for the amount handed over and writes it off the balance. False when
// cancelled, refused, or written.
bool showSettleDebtDialog(QWidget* parent, app::data::Database& db, int customerId,
                          long long currentBalance);

// The whole card: the info tab plus the credit sales, the repayments and the
// merged history. Returns true when something was saved or recorded inside, so
// the page knows to reload the list.
bool showCustomerCardDialog(QWidget* parent, app::data::Database& db, const core::Customer& customer);

} // namespace app::ui
