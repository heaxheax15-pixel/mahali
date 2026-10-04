#pragma once

#include <optional>

#include "core/product.h"

class QWidget;

namespace app::data {
class Database;
}

namespace app::ui {

// Product create/edit form, shared by the products page and the POS register.
// Returns the product as filled in, or nothing when the dialog is cancelled.
//
// Which mode it opens in is taken from initial.id: id 0 means a new product,
// a non-zero id means that row is being edited.
//
// Only the sale price is mandatory. A product with no name and no barcode is a
// legitimate quick item, so the form does not turn the cashier away over an
// empty field other than the price.
std::optional<core::Product> showProductDialog(QWidget* parent, app::data::Database& db,
                                               const core::Product& initial = {});

// How a product leaves the catalogue, asked and carried out in one place.
//
// A product nothing has ever been done to is erased, after a confirmation that
// says the act cannot be undone. A product with a sale, a purchase or any other
// document behind it cannot be erased without taking the meaning out of that
// document, so it is deactivated instead -- the operator is told this in as many
// words and asked to confirm, and the row stays where the reports can still read
// it.
//
// Returns true when the product left the catalogue, erased or deactivated.
// Returns false when the operator backed out, or the write failed and the
// product is exactly as it was. The repository has done the writing by the time
// this returns, so a caller that wants the grid repainted refreshes on true.
bool confirmProductRemoval(QWidget* parent, app::data::Database& db, const core::Product& product);

} // namespace app::ui
