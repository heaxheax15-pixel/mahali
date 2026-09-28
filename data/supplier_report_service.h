#pragma once

#include <QString>
#include <QVector>

#include "core/purchase.h"
#include "core/supplier.h"
#include "core/supplier_payment.h"
#include "core/supplier_return.h"
#include "database.h"
#include "purchase_repository.h"
#include "supplier_payment_repository.h"
#include "supplier_repository.h"
#include "supplier_return_repository.h"

namespace app::data {

// Where one supplier stands, broken into the parts that produced the number. A
// single balance cannot be argued with: an operator who disputes it needs to see
// which of the four terms is wrong.
struct SupplierSummary {
    core::Supplier supplier;
    // What the shop already owed before any recorded transaction.
    long long openingBalanceCents = 0;
    long long purchasesTotalCents = 0;
    long long paymentsTotalCents = 0;
    long long returnsTotalCents = 0;
    // opening + purchases - payments - returns. Negative means the shop is ahead
    // of the supplier, which is a real state and is left negative on purpose.
    long long currentBalanceCents = 0;
    int purchaseCount = 0;
    // Invoices with something still outstanding on them. Counted separately from
    // the balance because a general payment can settle several invoices at once
    // without ever being attached to one of them.
    int unpaidInvoiceCount = 0;
    long long unpaidTotalCents = 0;
};

struct SupplierReport {
    QVector<SupplierSummary> suppliers;
    // What the shop owes in total. Only positive balances are added: a negative
    // one is money the supplier owes the shop, and netting the two together would
    // hide both.
    long long grandTotalOwedCents = 0;
    int totalSuppliers = 0;
};

// Everything one supplier did inside a window, with the lines kept so an operator
// can read the totals against them instead of taking the sum on trust.
struct SupplierPeriodReport {
    int supplierId = 0;
    QString fromIso;
    QString toIso;
    long long purchasesCents = 0;
    long long paymentsCents = 0;
    long long returnsCents = 0;
    // purchases - payments - returns. What the period did to what is owed.
    long long netChangeCents = 0;
    QVector<core::Purchase> purchases;
    QVector<core::SupplierPayment> payments;
    QVector<core::SupplierReturn> returns;
};

class SupplierReportService {
public:
    SupplierReportService(Database& db,
                          SupplierRepository& suppliers,
                          PurchaseRepository& purchases,
                          SupplierPaymentRepository& payments,
                          SupplierReturnRepository& returns);

    // Every active supplier with what is owed to each. Inactive suppliers are left
    // out: they are not being dealt with, and a retired supplier's old balance
    // would keep showing up in a total owed today.
    SupplierReport summary() const;

    // One supplier's activity between two moments. An unknown supplier gives an
    // empty report with a zeroed supplierId, since a caller has nothing to show
    // for it either way.
    SupplierPeriodReport periodReport(int supplierId,
                                      const QString& fromIso,
                                      const QString& toIso) const;

private:
    Database& m_db;
    SupplierRepository& m_suppliers;
    PurchaseRepository& m_purchases;
    SupplierPaymentRepository& m_payments;
    SupplierReturnRepository& m_returns;
};

} // namespace app::data
