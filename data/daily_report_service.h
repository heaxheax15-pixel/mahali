#pragma once

#include <QString>
#include <QVector>

#include "cash_session_repository.h"
#include "customer_transaction_repository.h"
#include "database.h"
#include "expense_repository.h"
#include "owner_drawing_repository.h"
#include "sale_repository.h"

namespace app::data {

// The best lines of a day, by units moved. Revenue is reported alongside the
// quantity because the two can disagree: a big-ticket line and a high-volume one
// are both worth knowing about and neither alone is "top".
struct TopProduct {
    int productId = 0;
    QString productName;
    long long quantitySold = 0;
    long long revenueCents = 0;
};

// A debt taken on during the day, as opposed to one settled. Only new ones are
// listed: a day's worth of repayments is already visible in the till.
struct NewDebt {
    int customerId = 0;
    QString customerName;
    long long amountCents = 0;
    QString createdAt;
};

// One day of trading, from takings to the till. Kept as one report because the
// numbers are only meaningful together: a day's profit says nothing about whether
// the cash for it was ever counted.
struct DailyReport {
    // The day asked for, as "YYYY-MM-DD", echoed back so a caller can label a
    // report without having to remember which day it built.
    QString dayIso;
    long long salesTotalCents = 0;
    // What the sold goods cost at the moment they were sold, taken from the cost
    // frozen on each line. Recomputing from today's average cost would restate
    // past sales at prices the shop never charged.
    long long cogsTotalCents = 0;
    long long grossProfitCents = 0;
    int invoiceCount = 0;
    long long expensesCents = 0;
    long long drawingsCents = 0;
    // gross - expenses - drawings. Drawings are subtracted because money the
    // owner took out is not the shop's profit even though it never counted as an
    // expense either.
    long long netProfitCents = 0;

    // The till for the day, from the session opened on it. All five are zero when
    // no session was opened, rather than absent, so a day traded without a till
    // reads as "not reconciled" instead of breaking the layout.
    long long cashOpeningCents = 0;
    // Zero while the session is still open: there is no closing count to report
    // yet, and zero is not a claim that the till came out empty.
    long long cashClosingCents = 0;
    long long cashExpectedCents = 0;
    long long cashActualCents = 0;
    // What was counted less what should have been there. Negative is a shortfall.
    long long cashDifferenceCents = 0;

    QVector<TopProduct> topProducts;
    QVector<NewDebt> newDebts;
};

class DailyReportService {
public:
    DailyReportService(Database& db,
                       SaleRepository& sales,
                       ExpenseRepository& expenses,
                       OwnerDrawingRepository& drawings,
                       CustomerTransactionRepository& customerTransactions,
                       CashSessionRepository& cashSessions);

    // The report for one day, given as "YYYY-MM-DD". Every figure defaults to
    // zero, so a day that was not traded has a report rather than no report.
    DailyReport forDay(const QString& dayIso) const;

private:
    Database& m_db;
    SaleRepository& m_sales;
    ExpenseRepository& m_expenses;
    OwnerDrawingRepository& m_drawings;
    CustomerTransactionRepository& m_customerTx;
    CashSessionRepository& m_cashSessions;
};

} // namespace app::data
