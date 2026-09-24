#include "report_service.h"

#include <QSqlQuery>
#include <QVariant>

#include "core/profit_loss_calculator.h"
#include "core/zakat_calculator.h"
#include "customer_repository.h"
#include "customer_transaction_repository.h"
#include "date_utils.h"
#include "expense_repository.h"
#include "owner_drawing_repository.h"
#include "payment_repository.h"
#include "sale_item_repository.h"
#include "sale_repository.h"

namespace app::data {

ReportService::ReportService(Database& db)
    : m_db(db)
{
}

namespace {

long long cogsFor(const std::vector<core::SaleItem>& items)
{
    long long cogs = 0;
    for (const core::SaleItem& item : items) {
        cogs += item.unitCostCents * item.quantity;
    }
    return cogs;
}

} // namespace

StoreReport ReportService::build(const QDateTime& from, const QDateTime& to) const
{
    StoreReport report;

    SaleRepository sales(m_db);
    SaleItemRepository saleItems(m_db);
    const auto allSales = sales.findBetween(from, to);
    report.salesCount = static_cast<long long>(allSales.size());
    for (const core::Sale& sale : allSales) {
        report.revenueCents += sale.totalCents;
        if (sale.reversedSaleId == 0) {
            report.cogsCents += cogsFor(saleItems.findBySaleId(sale.id));
        }
    }

    ExpenseRepository expenses(m_db);
    for (const core::Expense& expense : expenses.findBetween(from, to)) {
        report.expensesCents += expense.amountCents;
    }

    OwnerDrawingRepository drawings(m_db);
    for (const core::OwnerDrawing& drawing : drawings.findBetween(from, to)) {
        report.drawingsCents += drawing.amountCents;
    }

    const core::ProfitLossReport pl =
        core::ProfitLossCalculator::compute(report.revenueCents, report.cogsCents, report.expensesCents,
                                            report.drawingsCents);
    report.grossProfitCents = pl.grossProfitCents;
    report.netProfitCents = pl.netProfitCents;

    // Outstanding customer balances today (whole history: debt minus payments).
    CustomerRepository customers(m_db);
    CustomerTransactionRepository transactions(m_db);
    PaymentRepository payments(m_db);
    for (const core::Customer& customer : customers.findAll()) {
        long long balance = 0;
        for (const core::CustomerTransaction& tx : transactions.findByCustomerId(customer.id)) {
            balance += tx.amountCents;
        }
        for (const core::Payment& payment : payments.findByCustomerId(customer.id)) {
            balance -= payment.amountCents;
        }
        if (balance > 0) {
            report.outstandingDebtCents += balance;
        }
    }
    report.zakatBaseCents =
        core::ZakatCalculator::zakatBaseCents(report.revenueCents, report.outstandingDebtCents);
    report.zakatCents = report.zakatBaseCents > 0 ? report.zakatBaseCents * 25 / 1000 : 0;

    QSqlQuery sessionQuery(m_db.handle());
    sessionQuery.prepare(QStringLiteral(
        "SELECT COUNT(*), COALESCE(SUM(opening_float_cents), 0) FROM cash_sessions "
        "WHERE opened_at >= ? AND opened_at <= ?"));
    sessionQuery.addBindValue(toIso(from));
    sessionQuery.addBindValue(toIso(to));
    if (sessionQuery.exec() && sessionQuery.next()) {
        report.sessionsOpened = sessionQuery.value(0).toLongLong();
        report.openingFloatCents = sessionQuery.value(1).toLongLong();
    }

    QSqlQuery cashQuery(m_db.handle());
    cashQuery.prepare(QStringLiteral(
        "SELECT type, COUNT(*), COALESCE(SUM(amount_cents), 0) FROM cash_movements "
        "WHERE created_at >= ? AND created_at <= ? GROUP BY type ORDER BY MIN(created_at)"));
    cashQuery.addBindValue(toIso(from));
    cashQuery.addBindValue(toIso(to));
    if (cashQuery.exec()) {
        while (cashQuery.next()) {
            CashLine line;
            line.type = cashQuery.value(0).toString();
            line.count = cashQuery.value(1).toInt();
            line.sumCents = cashQuery.value(2).toLongLong();
            report.cashLines.push_back(line);
        }
    }

    return report;
}

} // namespace app::data