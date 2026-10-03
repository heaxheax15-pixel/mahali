#include "report_service.h"

#include <QDebug>
#include <QSqlQuery>
#include <QVariant>

#include <limits>
#include <optional>

#include "core/profit_loss_calculator.h"
#include "core/zakat_calculator.h"
#include "core/cash_session_calculator.h"
#include "cash_movement_repository.h"
#include "cash_session_repository.h"
#include "customer_repository.h"
#include "customer_transaction_repository.h"
#include "date_utils.h"
#include "expense_repository.h"
#include "owner_drawing_repository.h"
#include "payment_repository.h"
#include "sale_item_repository.h"
#include "sale_repository.h"
#include "setting_repository.h"
#include "supplier_repository.h"


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

long long stockValueCents(Database& db)
{
    QSqlQuery query(db.handle());
    query.prepare(QStringLiteral("SELECT id, quantity, sale_price_cents FROM products "
                                 "WHERE active = 1 AND quantity > 0"));
    if (!query.exec()) {
        db.recordError(query.lastError(), QStringLiteral("ReportService::stockValueCents"));
        return 0;
    }

    const long long ceiling = std::numeric_limits<long long>::max();
    long long total = 0;
    while (query.next()) {
        const long long quantity = query.value(1).toLongLong();
        const long long price = query.value(2).toLongLong();
        if (quantity > 0 && price > ceiling / quantity) {
            qWarning() << "stock value: product" << query.value(0).toInt()
                       << "left out, its quantity times its sale price overflows";
            continue;
        }
        const long long term = quantity * price;
        if (total > ceiling - term) {
            qWarning() << "stock value: stopped at product" << query.value(0).toInt()
                       << ", the running total would overflow";
            break;
        }
        total += term;
    }
    return total;
}

long long cashOnHandCents(Database& db)
{
    CashSessionRepository sessions(db);
    const std::optional<core::CashSession> session = sessions.findOpen();
    if (!session.has_value()) {
        return 0;
    }
    CashMovementRepository movements(db);
    return core::CashSessionCalculator::expectedTotalCents(session->openingFloatCents,
                                                           movements.sumBySessionId(session->id));
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
    report.receivablesCents = report.outstandingDebtCents;

    core::ZakatInputs zakatInputs;
    report.stockValueCents = stockValueCents(m_db);
    report.cashOnHandCents = cashOnHandCents(m_db);
    zakatInputs.stockValueCents = report.stockValueCents;
    zakatInputs.cashCents = report.cashOnHandCents;
    zakatInputs.receivablesCents = report.receivablesCents;
    report.zakatBaseCents = core::ZakatCalculator::zakatBase(zakatInputs);

    SupplierRepository suppliers(m_db);
    for (const core::Supplier& supplier : suppliers.findAll()) {
        const long long balance = suppliers.balanceCentsFor(supplier.id);
        if (balance > 0) {
            report.supplierDebtCents += balance;
        }
    }

    SettingRepository settings(m_db);
    const auto enabledRow = settings.value(QStringLiteral("enabled"));
    const bool zakatEnabled = !enabledRow.has_value() || *enabledRow == QLatin1String("1");
    report.zakatCents = zakatEnabled && report.zakatBaseCents > 0 ? report.zakatBaseCents * 25 / 1000 : 0;

    const long long nisabCents =
        settings.value(QStringLiteral("nisab_cents")).value_or(QString()).toLongLong();
    if (nisabCents > 0 && report.zakatBaseCents < nisabCents) {
        report.zakatCents = 0;
    }

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