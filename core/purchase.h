#pragma once

#include <optional>
#include <QString>

#include "supplier_payment.h"

namespace app::core {

struct Purchase {
    int id = 0;
    int supplierId = 0;
    QString invoiceNumber;
    QString purchasedAt;
    long long subtotalCents = 0;
    long long vatCents = 0;
    long long totalCents = 0;
    long long paidCents = 0;
    // How the paid part was settled. Cash means a cash_movements row was written;
    // Credit/Bank settled the balance without touching the drawer.
    core::SupplierPaymentMethod method = core::SupplierPaymentMethod::Cash;
    bool addToStock = true;
    QString note;
    std::optional<int> occasionId;
    QString createdAt;
    // Links a void to the original purchase. 0 means "not a void".
    int reversedId = 0;
};

} // namespace app::core