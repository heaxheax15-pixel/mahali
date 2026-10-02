#include "daily_report_service.h"

#include <QDateTime>
#include <QSqlQuery>

#include "date_utils.h"

namespace app::data {

DailyReportService::DailyReportService(Database& db,
                                       SaleRepository& sales,
                                       ExpenseRepository& expenses,
                                       OwnerDrawingRepository& drawings,
                                       CustomerTransactionRepository& customerTransactions,
                                       CashSessionRepository& cashSessions)
    : m_db(db)
    , m_sales(sales)
    , m_expenses(expenses)
    , m_drawings(drawings)
    , m_customerTx(customerTransactions)
    , m_cashSessions(cashSessions)
{
}

DailyReport DailyReportService::forDay(const QString& dayIso) const
{
    DailyReport report;
    report.dayIso = dayIso;

    // The day is named as a bare date, so both ends are widened to cover it whole.
    // Without this the closing bound would sort before any timestamp of that same
    // day and the day's later sales would go missing.
    const QString from = widenToDayStart(dayIso);
    const QString to = widenToDayEnd(dayIso);

    // Takings and the count of documents are read in one pass over the same rows.
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT COALESCE(SUM(total_cents), 0), COUNT(*) FROM sales "
                                 "WHERE created_at >= ? AND created_at <= ?"));
    query.addBindValue(from);
    query.addBindValue(to);
    if (query.exec() && query.next()) {
        report.salesTotalCents = query.value(0).toLongLong();
        report.invoiceCount = query.value(1).toInt();
    }

    // Cost of goods comes off the cost frozen on each line, not off the product's
    // current average cost: a sale made last month at yesterday's price must not
    // be restated at today's.
    query.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(si.unit_cost_cents * si.quantity), 0) "
        "FROM sale_items si "
        "JOIN sales s ON s.id = si.sale_id "
        "WHERE s.created_at >= ? AND s.created_at <= ?"));
    query.addBindValue(from);
    query.addBindValue(to);
    if (query.exec() && query.next()) {
        report.cogsTotalCents = query.value(0).toLongLong();
    }

    report.grossProfitCents = report.salesTotalCents - report.cogsTotalCents;

    // The two repositories are asked for moments rather than text, so the widened
    // bounds are read back. A day that will not parse has no rows to collect.
    const std::optional<QDateTime> fromDt = data::fromIso(from);
    const std::optional<QDateTime> toDt = data::fromIso(to);
    if (fromDt.has_value() && toDt.has_value()) {
        for (const core::Expense& expense : m_expenses.findBetween(*fromDt, *toDt)) {
            report.expensesCents += expense.amountCents;
        }
        for (const core::OwnerDrawing& drawing : m_drawings.findBetween(*fromDt, *toDt)) {
            report.drawingsCents += drawing.amountCents;
        }
    }

    report.netProfitCents = report.grossProfitCents - report.expensesCents - report.drawingsCents;

    // The till. A day with no session leaves every figure at zero, which reads as
    // "never counted" rather than as "counted nothing".
    if (const std::optional<core::CashSession> session = m_cashSessions.findForDay(dayIso)) {
        report.cashOpeningCents = session->openingFloatCents;
        // Only a closed session has a counted figure to publish. While it is open
        // the field stays zero, which is not a claim the till came out empty.
        if (session->status == QStringLiteral("closed")) {
            report.cashClosingCents = session->closingCountedCents;
            report.cashExpectedCents = session->expectedCents;
            report.cashActualCents = session->closingCountedCents;
            report.cashDifferenceCents = session->varianceCents;
        }
    }

    query.prepare(QStringLiteral(
        "SELECT type, COUNT(*), COALESCE(SUM(amount_cents), 0) FROM cash_movements "
        "WHERE created_at >= ? AND created_at <= ? GROUP BY type ORDER BY MIN(created_at)"));
    query.addBindValue(from);
    query.addBindValue(to);
    if (query.exec()) {
        while (query.next()) {
            DailyCashLine line;
            line.type = query.value(0).toString();
            line.count = query.value(1).toInt();
            line.sumCents = query.value(2).toLongLong();
            report.cashLines.push_back(line);
        }
    }

    // Best sellers by units, five of them. sale_items stores no line total, so
    // revenue is worked out from the quantity and the price the line was sold at.
    query.prepare(QStringLiteral(
        "SELECT si.product_id, p.name, SUM(si.quantity), SUM(si.quantity * si.unit_price_cents) "
        "FROM sale_items si "
        "JOIN sales s ON s.id = si.sale_id "
        "JOIN products p ON p.id = si.product_id "
        "WHERE s.created_at >= ? AND s.created_at <= ? "
        "GROUP BY si.product_id "
        "ORDER BY SUM(si.quantity) DESC, si.product_id "
        "LIMIT 5"));
    query.addBindValue(from);
    query.addBindValue(to);
    if (query.exec()) {
        while (query.next()) {
            TopProduct top;
            top.productId = query.value(0).toInt();
            top.productName = query.value(1).toString();
            top.quantitySold = query.value(2).toLongLong();
            top.revenueCents = query.value(3).toLongLong();
            report.topProducts.push_back(top);
        }
    }

    // Debts taken on during the day. customer_transactions has no type column, the
    // sign of the amount is the direction: a positive row is what the customer now
    // owes, a negative one is what they paid.
    //
    // Every row in the window is listed, signs and all. A reversal is written as
    // a negative row against the original (that is what reversed_transaction_id
    // marks it with), so keeping only amount_cents > 0 would hide the reversal
    // and list the cancelled credit sale as a debt taken today — the customer
    // would appear to owe something that was undone before the day was out.
    query.prepare(QStringLiteral(
        "SELECT ct.customer_id, c.name, ct.amount_cents, ct.created_at "
        "FROM customer_transactions ct "
        "JOIN customers c ON c.id = ct.customer_id "
        "WHERE ct.created_at >= ? AND ct.created_at <= ? "
        "ORDER BY ct.created_at DESC"));
    query.addBindValue(from);
    query.addBindValue(to);
    if (query.exec()) {
        while (query.next()) {
            NewDebt debt;
            debt.customerId = query.value(0).toInt();
            debt.customerName = query.value(1).toString();
            debt.amountCents = query.value(2).toLongLong();
            debt.createdAt = query.value(3).toString();
            report.newDebts.push_back(debt);
        }
    }

    return report;
}

} // namespace app::data
