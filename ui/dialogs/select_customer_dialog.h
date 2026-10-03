#pragma once

#include <optional>

#include <QWidget>

#include "data/database.h"

namespace app::ui {

// Picks the customer a credit sale is put on. Nothing when the dialog is closed
// without a choice, the chosen id when a row is taken.
//
// Every account is listed, closed ones included: a sale against someone the shop
// has stopped serving is exactly the case where what they still owe is the reason
// for opening the list. Deepest debt first, so the account being chased is the one
// under the cashier's eye rather than wherever the name happens to sort.
std::optional<int> showSelectCustomerDialog(QWidget* parent, app::data::Database& db);

} // namespace app::ui
