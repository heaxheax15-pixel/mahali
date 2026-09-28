#pragma once

#include <optional>
#include <QString>
#include <QVector>

#include "core/purchase.h"
#include "database.h"
#include "purchase_repository.h"
#include "supplier_payment_repository.h"
#include "supplier_repository.h"

namespace app::data {

struct SupplierPaymentResult {
    bool ok = false;
    int paymentId = 0;
    QString error;
};

// Records what was paid to a supplier, and answers what is still owed.
class SupplierPaymentService {
public:
    SupplierPaymentService(Database& db,
                           SupplierPaymentRepository& payments,
                           SupplierRepository& suppliers,
                           PurchaseRepository& purchases);

    // Records a payment to a supplier. purchaseId is optional: when it is given
    // the payment is against that invoice and the invoice must belong to the
    // same supplier; when it is empty the payment is general, taken off the
    // balance as a whole.
    SupplierPaymentResult recordPayment(int supplierId,
                                        std::optional<int> purchaseId,
                                        long long amountCents,
                                        const QString& paidAt,
                                        const QString& note);

    // The invoices of one supplier that are not settled in full, oldest first.
    // "Not settled in full" means the invoice total is above everything paid
    // against it.
    struct UnpaidInvoice {
        core::Purchase purchase;
        long long remainingCents = 0;
    };
    QVector<UnpaidInvoice> unpaidInvoicesFor(int supplierId) const;

    // What the supplier is owed overall, straight from the repository.
    long long balanceFor(int supplierId) const;

private:
    Database& m_db;
    SupplierPaymentRepository& m_payments;
    SupplierRepository& m_suppliers;
    PurchaseRepository& m_purchases;
};

} // namespace app::data
