#pragma once

#include <QWidget>

namespace app::data {
class Database;
}

namespace app::ui {

// The annual zakat reminder: asks for today's gold price, turns it into a nisab
// with the 85-gram threshold, and shows what the shop's trading goods are worth
// against it.
//
// Three ways out, and they deliberately write different amounts:
//   - "Enregistrer le Nisab" stores the threshold alone and keeps the dialog
//     open, for a shop that only wants the threshold on file.
//   - "Marquer comme payée" records the payment for the current zakat year, and
//     only appears once the base has reached the nisab.
//   - "Plus tard" writes nothing at all.
void showZakatDialog(QWidget* parent, app::data::Database& db);

} // namespace app::ui