#pragma once

#include <optional>
#include <QString>

namespace app::core {

// How a payment to a supplier left the shop.
//
// The distinction is not cosmetic: only Cash has a cash_movements row behind it.
// Settling an account or paying through the bank moves the supplier's balance
// and leaves the drawer exactly as it was, and before this existed a payment had
// no way to say which of the three it was — so no report could add it up and a
// day with a large supplier payment on it reconciled short.
enum class SupplierPaymentMethod {
    Cash,
    Credit,
    Bank,
};

// The stored spelling. Kept as the wire form so a row read back and written
// again is byte-identical, and so the column can be inspected in a shell.
inline QString supplierPaymentMethodName(SupplierPaymentMethod method)
{
    switch (method) {
    case SupplierPaymentMethod::Cash:
        return QStringLiteral("cash");
    case SupplierPaymentMethod::Credit:
        return QStringLiteral("credit");
    case SupplierPaymentMethod::Bank:
        return QStringLiteral("bank");
    }
    return QStringLiteral("cash");
}

// Reads a stored spelling back. Absent for anything else, so a row written by a
// future build with a method this one does not know surfaces as "not recognised"
// instead of being silently treated as cash — which would put a drawer movement
// behind a payment that never touched the drawer.
inline std::optional<SupplierPaymentMethod> parseSupplierPaymentMethod(const QString& name)
{
    if (name == QLatin1String("cash")) {
        return SupplierPaymentMethod::Cash;
    }
    if (name == QLatin1String("credit")) {
        return SupplierPaymentMethod::Credit;
    }
    if (name == QLatin1String("bank")) {
        return SupplierPaymentMethod::Bank;
    }
    return std::nullopt;
}

// True for the one method that puts money through the drawer.
inline bool isCashPayment(SupplierPaymentMethod method)
{
    return method == SupplierPaymentMethod::Cash;
}

// Money paid to a supplier. A NULL purchaseId is a general payment that is not
// tied to one invoice; a set purchaseId is a payment against that invoice.
struct SupplierPayment {
    int id = 0;
    int supplierId = 0;
    std::optional<int> purchaseId;
    long long amountCents = 0;
    QString paidAt;
    // What the operator chose. Cash is the default because it is what the form
    // leads with, and because every row written before the column existed was a
    // payment taken at the counter.
    SupplierPaymentMethod method = SupplierPaymentMethod::Cash;
    QString note;
    QString createdAt;
    // Links a reversal to the original payment. 0 means "not a reversal". The
    // partial unique index (migration 3) guarantees at most one reversal per
    // original.
    int reversedId = 0;
    bool isPurchaseInitialPayment = false;
};

} // namespace app::core