#pragma once

#include <optional>
#include <QString>

namespace app::core {

// Money paid to a supplier. A NULL purchaseId is a general payment that is not
// tied to one invoice; a set purchaseId is a payment against that invoice.
struct SupplierPayment {
    int id = 0;
    int supplierId = 0;
    std::optional<int> purchaseId;
    long long amountCents = 0;
    QString paidAt;
    QString note;
    QString createdAt;
};

} // namespace app::core
