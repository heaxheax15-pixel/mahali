#pragma once

#include <optional>
#include <QString>
#include <QVector>

#include "core/purchase.h"
#include "core/supplier_payment.h"
#include "cash_movement_repository.h"
#include "cash_session_repository.h"
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
    //
    // method says how the money left the shop. Only Cash moves the drawer, and
    // only then is cashSessionId needed: cash_movements.session_id is NOT NULL,
    // so a cash payment with no session cannot be recorded at all and is refused
    // outright rather than written with no till behind it. Credit and Bank settle
    // the supplier's balance and leave the drawer alone, so they need no session
    // and are refused by nothing.
    //
    // The session is checked inside the transaction rather than when the form
    // was opened: a till closed while the operator was typing must not be written
    // to. The id is passed in rather than looked up, so the movement is
    // attributed to the session the caller meant and never to whichever one
    // happened to be open.
SupplierPaymentResult recordPayment(int supplierId,
                                         std::optional<int> purchaseId,
                                         long long amountCents,
                                         const QString& paidAt,
                                         const QString& note,
                                         core::SupplierPaymentMethod method =
                                             core::SupplierPaymentMethod::Cash,
                                         std::optional<int> cashSessionId = std::nullopt);

    // Cancels a supplier payment. Same shape as PaymentRepository::reverse for the
    // same reason: nothing is edited or deleted, a negative row is written against
    // the original and the balance comes back. If the original was cash, the money
    // goes back into the named session.
    //
    // cashSessionId is required when the original was Cash — the reversal is a
    // positive movement into that session, and the drawer has to be able to count
    // it. For Credit/Bank originals the parameter is ignored (no cash moved).
    SupplierPaymentResult reversePayment(int paymentId, std::optional<int> cashSessionId);

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
