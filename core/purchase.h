#pragma once

#include <optional>
#include <QString>

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
    bool addToStock = true;
    QString note;
    std::optional<int> occasionId;
    QString createdAt;
};

} // namespace app::core