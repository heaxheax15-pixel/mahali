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
#include "customer_transaction_item_repository.h"
#include "date_utils.h"
#include "expense_repository.h"
#include "owner_drawing_repository.h"
#include "payment_repository.h"
#include "sale_item_repository.h"
#include "sale_rules.h"
#include "sale_repository.h"
#include "setting_repository.h"
#include "supplier_repository.h"


namespace app::data {

ReportService::ReportService(Database& db)
    : m_db(db)
{
}

namespace {

// Signed, so a reversal's negative quantities take the original's cost back out
// and a cancelled sale nets to nothing. Guarded per line and per running total
// because the terms are now negative as well as positive: a wrapped cost is not
// a wrong cost, it is a plausible wrong amount of money, and it reaches the
// profit and loss statement.
long long cogsFor(const std::vector<core::SaleItem>& items)
{
    long long cogs = 0;
    // unitCostCents is per piece, so the multiplier must be the piece count.
    // Using quantity here would under-count COGS on a carton sale by a factor
    // of pieces_per_package, and the shop would report a profit it did not make.
    // These rows are read back off the ledger rather than off the resolved vector,
    // which is why the multiplier is spelled out here and not inherited.
    for (const core::SaleItem& item : items) {
        if (!detail::productFits(item.unitCostCents, item.piecesConsumed)) {
            qWarning() << "sale cost: item" << item.id
                       << "left out, its quantity times its unit cost overflows";
            continue;
        }
        long long term = item.unitCostCents * item.piecesConsumed;
        // pieces_consumed is always a positive count (it is the number of pieces
        // involved, not a signed amount), so its sign must not be taken from here.
        // The direction of the line lives on quantity: a reversal stores quantity
        // negative and pieces_consumed positive, because the count answers "how many
        // pieces moved" while the ledger answers "which way". Reading magnitude from
        // one field and direction from the other is the only combination that nets
        // a reversal to zero.
        if (item.quantity < 0) {
            // The most negative long long has no positive counterpart, so negating
            // it wraps to itself. A wrapped cost is not a wrong cost, it is a
            // plausible wrong amount of money, so the line is left out of the sum
            // and named in the log rather than counted at the wrong figure.
            if (term == std::numeric_limits<long long>::min()) {
                qWarning() << "sale cost: item" << item.id
                           << "left out, its negated cost would overflow";
                continue;
            }
            term = -term;
        }
        if (!detail::sumFits(cogs, term)) {
            qWarning() << "sale cost: stopped at item" << item.id
                       << ", the running total would overflow";
            break;
        }
        cogs += term;
    }
    return cogs;
}

// Cost of goods that went out on account, summed the same way and for the same
// reason: signed, so a cancellation's negative quantities take the original back
// out and a reversed sale lands on zero. Deliberately no "skip the cancellation"
// test — the cancellation carries its own items, exactly as the cash reversal
// does, and skipping them would count the cost of goods that came back.
long long creditCogsFor(const std::vector<core::CustomerTransactionItem>& lines)
{
    long long cogs = 0;
    // unitCostCents is per piece, so the multiplier must be the piece count.
    // Using quantity here would under-count COGS on a carton sold on account by a
    // factor of pieces_per_package, and the shop would report a profit it did not
    // make. A credit sale moves stock exactly as a cash one does, so it is counted
    // the same way.
    for (const core::CustomerTransactionItem& line : lines) {
        if (!detail::productFits(line.unitCostCents, line.piecesConsumed)) {
            qWarning() << "credit sale cost: item" << line.id
                       << "left out, its quantity times its unit cost overflows";
            continue;
        }
        long long term = line.unitCostCents * line.piecesConsumed;
        // Signed the same way as the cash path above: magnitude from
        // pieces_consumed, direction from quantity. A cancellation on account
        // stores quantity negative and pieces_consumed positive, exactly as a cash
        // reversal does, because a credit sale moves the same goods.
        if (line.quantity < 0) {
            // No positive counterpart to negate into, so the line is left out and
            // named rather than counted at a wrapped figure.
            if (term == std::numeric_limits<long long>::min()) {
                qWarning() << "credit sale cost: item" << line.id
                           << "left out, its negated cost would overflow";
                continue;
            }
            term = -term;
        }
        // Each term in range is not enough: the running sum can leave it on its
        // own once enough lines are added together.
        if (!detail::sumFits(cogs, term)) {
            qWarning() << "credit sale cost: stopped at item" << line.id
                       << ", the running total would overflow";
            break;
        }
        cogs += term;
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
    for (const core::Sale& sale : allSales) {
        report.revenueCents += sale.totalCents;
        // Net, like the revenue above it: a reversal undoes a sale, so it takes
        // one back off. Counting every row would report two sales for one that
        // was undone, and a figure that rises when a sale is cancelled is not a
        // count of anything.
        report.salesCount += sale.reversedSaleId == 0 ? 1 : -1;
        // Added for every sale, reversal rows included. A reversal carries its own
        // items with negative quantities, so their cost is what takes the original
        // back out; skipping the reversal left the cost of goods that were on the
        // shelf again sitting in the profit and loss statement, while the revenue
        // above had already been cancelled. Same summing as the credit path below,
        // and for the same reason.
        report.cogsCents += cogsFor(saleItems.findBySaleId(sale.id));
    }

    // Credit sales happened too, in the same period, and left out they made
    // "revenue" mean only what the till took rather than what the shop sold.
    //
    // They live in customer_transactions: a positive amount is debt taken on, and
    // a negative one carrying reversed_transaction_id is a cancellation. Customer
    // payments are deliberately not rows here — they go to the payments table —
    // so summing the signed amounts nets a cancellation out without any risk of
    // subtracting a payment from revenue.
    //
    // Zakat does not move. Its base is stock + cash + receivables and has never
    // read revenue; a sale on account shows up there through the receivable.
    CustomerTransactionRepository creditTx(m_db);
    CustomerTransactionItemRepository creditItems(m_db);
    for (const core::CustomerTransaction& tx : creditTx.findBetween(from, to)) {
        report.revenueCents += tx.amountCents;
        // Net, like the revenue above it: a cancellation undoes a sale, so it
        // takes one back off rather than being counted as one. Counting every row
        // would report two sales for one that was undone, and a figure that rises
        // when a sale is cancelled is not a count of anything.
        //
        // Floored at zero because the count is read off a card: a cancellation
        // whose sale fell in an earlier period has nothing here to take back off,
        // and "-1 ventes" would be a worse answer than 0. The revenue line is not
        // floored — it carries the reversal honestly.
        report.salesCount += tx.amountCents > 0 ? 1 : -1;
        report.cogsCents += creditCogsFor(creditItems.findByTransactionId(tx.id));
    }
    if (report.salesCount < 0) {
        report.salesCount = 0;
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