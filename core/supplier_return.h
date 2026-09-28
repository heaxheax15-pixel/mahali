#pragma once

#include <optional>
#include <QString>

namespace app::core {

// Goods given back to a supplier. This is money coming off what is owed to them,
// so the balance subtracts it. A NULL purchaseId is a return not tied to one
// invoice; a set purchaseId is a return against that invoice.
struct SupplierReturn {
    int id = 0;
    int supplierId = 0;
    std::optional<int> purchaseId;
    long long amountCents = 0;
    QString returnedAt;
    // Whether the returned goods leave the shelves as well. A return for credit
    // only leaves the stock alone, which is why this is recorded per return
    // rather than assumed.
    bool removeFromStock = true;
    QString note;
    QString createdAt;
};

} // namespace app::core
