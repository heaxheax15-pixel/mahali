#include "supplier_report_service.h"

#include <QDateTime>
#include <QSqlQuery>

#include "date_utils.h"

namespace app::data {

namespace {

// The stored timestamps are ISO text, so an inclusive window is two string
// comparisons. The bound is not copied out of the struct, because a return's own
// text is the thing being judged.
bool withinRange(const QString& value, const QString& fromIso, const QString& toIso)
{
    if (value < fromIso) {
        return false;
    }
    if (value > toIso) {
        return false;
    }
    return true;
}

} // namespace

SupplierReportService::SupplierReportService(Database& db,
                                             SupplierRepository& suppliers,
                                             PurchaseRepository& purchases,
                                             SupplierPaymentRepository& payments,
                                             SupplierReturnRepository& returns)
    : m_db(db)
    , m_suppliers(suppliers)
    , m_purchases(purchases)
    , m_payments(payments)
    , m_returns(returns)
{
}

SupplierReport SupplierReportService::summary() const
{
    SupplierReport report;

    const QVector<core::Supplier> suppliers = m_suppliers.listActive();
    report.totalSuppliers = suppliers.size();

    for (const core::Supplier& supplier : suppliers) {
        SupplierSummary row;
        row.supplier = supplier;
        row.openingBalanceCents = supplier.openingBalanceCents;

        // Every figure is read as one aggregate rather than by loading the rows
        // and adding them up here, so a supplier with years of invoices does not
        // have to be pulled into memory to answer a balance question.
        QSqlQuery query(m_db.handle());
        query.prepare(QStringLiteral("SELECT COALESCE(SUM(total_cents), 0) FROM purchases WHERE supplier_id = ?"));
        query.addBindValue(supplier.id);
        if (query.exec() && query.next()) {
            row.purchasesTotalCents = query.value(0).toLongLong();
        }

        query.prepare(QStringLiteral("SELECT COALESCE(SUM(amount_cents), 0) FROM supplier_payments "
                                     "WHERE supplier_id = ?"));
        query.addBindValue(supplier.id);
        if (query.exec() && query.next()) {
            row.paymentsTotalCents = query.value(0).toLongLong();
        }

        query.prepare(QStringLiteral("SELECT COALESCE(SUM(amount_cents), 0) FROM supplier_returns "
                                     "WHERE supplier_id = ?"));
        query.addBindValue(supplier.id);
        if (query.exec() && query.next()) {
            row.returnsTotalCents = query.value(0).toLongLong();
        }

        query.prepare(QStringLiteral("SELECT COUNT(*) FROM purchases WHERE supplier_id = ?"));
        query.addBindValue(supplier.id);
        if (query.exec() && query.next()) {
            row.purchaseCount = query.value(0).toInt();
        }

        // What is left on each invoice, worked out per invoice rather than by
        // subtracting the supplier's payments from their invoices in bulk. A
        // payment tied to one invoice may not cover the whole of it, and a
        // general payment with no invoice of its own belongs to none of them.
        query.prepare(QStringLiteral(
            "SELECT COUNT(*), COALESCE(SUM(remaining), 0) FROM ("
            "  SELECT p.total_cents - COALESCE(SUM(sp.amount_cents), 0) AS remaining"
            "  FROM purchases p"
            "  LEFT JOIN supplier_payments sp ON sp.purchase_id = p.id"
            "  WHERE p.supplier_id = ?"
            "  GROUP BY p.id"
            "  HAVING remaining > 0"
            ")"));
        query.addBindValue(supplier.id);
        if (query.exec() && query.next()) {
            row.unpaidInvoiceCount = query.value(0).toInt();
            row.unpaidTotalCents = query.value(1).toLongLong();
        }

        row.currentBalanceCents = row.openingBalanceCents + row.purchasesTotalCents
                                  - row.paymentsTotalCents - row.returnsTotalCents;

        // Only what the shop owes is added. A negative balance is money coming the
        // other way, and netting it in would leave a total that matches no single
        // supplier on the list.
        if (row.currentBalanceCents > 0) {
            report.grandTotalOwedCents += row.currentBalanceCents;
        }

        report.suppliers.push_back(row);
    }

    return report;
}

SupplierPeriodReport SupplierReportService::periodReport(int supplierId,
                                                         const QString& fromIso,
                                                         const QString& toIso) const
{
    SupplierPeriodReport report;
    report.supplierId = supplierId;
    report.fromIso = fromIso;
    report.toIso = toIso;

    // Checked before any summing: a report for a supplier that does not exist
    // would otherwise come back as a row of zeroes, which reads like a quiet
    // period rather than a wrong id.
    if (!m_suppliers.findById(supplierId).has_value()) {
        return report;
    }

    const QString from = widenToDayStart(fromIso);
    const QString to = widenToDayEnd(toIso);

    // The payments repository is asked for moments rather than text, so the
    // widened bounds have to be read back. A bound that will not parse leaves
    // the window undefined, and an undefined window has no rows in it. Qualified
    // because the two parameters above are named after this function.
    const std::optional<QDateTime> fromDt = data::fromIso(from);
    const std::optional<QDateTime> toDt = data::fromIso(to);
    if (!fromDt.has_value() || !toDt.has_value()) {
        return report;
    }

    // The repositories filter by date but not by supplier, so the rows are
    // narrowed to this supplier here. The loads are the supplier's own period,
    // which is small enough to hold.
    for (const core::Purchase& purchase : m_purchases.findBetween(from, to)) {
        if (purchase.supplierId == supplierId) {
            report.purchases.push_back(purchase);
            report.purchasesCents += purchase.totalCents;
        }
    }

    for (const core::SupplierPayment& payment : m_payments.findBetween(*fromDt, *toDt)) {
        if (payment.supplierId == supplierId) {
            report.payments.push_back(payment);
            report.paymentsCents += payment.amountCents;
        }
    }

    // The returns repository has no range query, so it is asked for the
    // supplier's whole history and the dates are judged in memory.
    for (const core::SupplierReturn& returned : m_returns.findBySupplierId(supplierId)) {
        if (!withinRange(returned.returnedAt, from, to)) {
            continue;
        }
        report.returns.push_back(returned);
        report.returnsCents += returned.amountCents;
    }

    report.netChangeCents = report.purchasesCents - report.paymentsCents - report.returnsCents;
    return report;
}

} // namespace app::data
