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

} // namespace app::ui
