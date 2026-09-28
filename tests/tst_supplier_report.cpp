#include <QtTest/QtTest>

#include <QTemporaryDir>

#include "data/date_utils.h"
#include "data/database.h"
#include "data/purchase_repository.h"
#include "data/supplier_payment_repository.h"
#include "data/supplier_report_service.h"
#include "data/supplier_return_repository.h"
#include "data/supplier_repository.h"

using namespace app;

namespace {

// A window that has already passed, so a fixture transaction lands at a known
// date rather than at whenever the suite happens to run.
QString pastIso(int daysAgo, const char* timeOfDay = "12:00:00.000")
{
    const QDateTime when = QDateTime::currentDateTime().addDays(-daysAgo);
    return QStringLiteral("%1T%2").arg(when.date().toString(Qt::ISODate), QLatin1String(timeOfDay));
}

struct Fixture {
    explicit Fixture(const QString& path)
        : db(path)
        , suppliers(db)
        , purchases(db)
        , payments(db)
        , returns(db)
        , service(db, suppliers, purchases, payments, returns)
    {
    }

    data::Database db;
    data::SupplierRepository suppliers;
    data::PurchaseRepository purchases;
    data::SupplierPaymentRepository payments;
    data::SupplierReturnRepository returns;
    data::SupplierReportService service;
};

int addSupplier(data::SupplierRepository& suppliers, const QString& name, long long openingBalanceCents = 0)
{
    core::Supplier supplier;
    supplier.name = name;
    supplier.openingBalanceCents = openingBalanceCents;
    return suppliers.save(supplier);
}

// Purchases and payments carry NOT NULL text columns that the structs leave as
// null strings, which the driver would bind as NULL.
int addPurchase(data::PurchaseRepository& purchases, int supplierId, long long totalCents,
                const QString& purchasedAt)
{
    core::Purchase purchase;
    purchase.supplierId = supplierId;
    purchase.invoiceNumber = QStringLiteral("");
    purchase.note = QStringLiteral("");
    purchase.purchasedAt = purchasedAt;
    purchase.totalCents = totalCents;
    purchase.subtotalCents = totalCents;
    purchase.createdAt = purchasedAt;
    return purchases.insert(purchase);
}

int addPayment(data::SupplierPaymentRepository& payments, int supplierId, long long amountCents,
               std::optional<int> purchaseId, const QString& paidAt)
{
    core::SupplierPayment payment;
    payment.supplierId = supplierId;
    payment.purchaseId = purchaseId;
    payment.amountCents = amountCents;
    payment.paidAt = paidAt;
    payment.createdAt = paidAt;
    return payments.insert(payment);
}

int addReturn(data::SupplierReturnRepository& returns, int supplierId, long long amountCents,
              std::optional<int> purchaseId, const QString& returnedAt)
{
    core::SupplierReturn returned;
    returned.supplierId = supplierId;
    returned.purchaseId = purchaseId;
    returned.amountCents = amountCents;
    returned.returnedAt = returnedAt;
    returned.createdAt = returnedAt;
    return returns.insert(returned);
}

} // namespace

class SupplierReportTest : public QObject
{
    Q_OBJECT

private slots:
    void summary_empty();
    void summary_one_supplier_no_transactions();
    void summary_one_supplier_full_cycle();
    void summary_multiple_suppliers();
    void summary_unpaid_invoice_count();
    void summary_ignores_inactive_supplier();
    void period_report_with_purchases_and_payments();
    void period_report_returns_and_net_change();
    void period_report_unknown_supplier_is_empty();

private:
    QTemporaryDir m_dir;
};

void SupplierReportTest::summary_empty()
{
    Fixture f(m_dir.filePath(QStringLiteral("summary_empty.sqlite")));

    const data::SupplierReport report = f.service.summary();
    QCOMPARE(report.totalSuppliers, 0);
    QCOMPARE(report.grandTotalOwedCents, 0LL);
    QVERIFY(report.suppliers.isEmpty());
}

void SupplierReportTest::summary_one_supplier_no_transactions()
{
    Fixture f(m_dir.filePath(QStringLiteral("summary_one_no_txn.sqlite")));
    QVERIFY(addSupplier(f.suppliers, QStringLiteral("مورد جديد"), 10000) > 0);

    const data::SupplierReport report = f.service.summary();
    QCOMPARE(report.totalSuppliers, 1);
    QCOMPARE(report.grandTotalOwedCents, 10000LL);

    const data::SupplierSummary& row = report.suppliers.first();
    QCOMPARE(row.openingBalanceCents, 10000LL);
    // Nothing has happened yet, so the three totals are zero rather than absent,
    // and the balance is the opening figure carried through untouched.
    QCOMPARE(row.purchasesTotalCents, 0LL);
    QCOMPARE(row.paymentsTotalCents, 0LL);
    QCOMPARE(row.returnsTotalCents, 0LL);
    QCOMPARE(row.currentBalanceCents, 10000LL);
    QCOMPARE(row.purchaseCount, 0);
    QCOMPARE(row.unpaidInvoiceCount, 0);
    QCOMPARE(row.unpaidTotalCents, 0LL);
}

void SupplierReportTest::summary_one_supplier_full_cycle()
{
    Fixture f(m_dir.filePath(QStringLiteral("summary_full_cycle.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد كامل"), 0);
    QVERIFY(supplierId > 0);

    const QString when = pastIso(30);
    const int purchaseId = addPurchase(f.purchases, supplierId, 50000, when);
    QVERIFY(purchaseId > 0);
    QVERIFY(addPayment(f.payments, supplierId, 20000, purchaseId, when) > 0);
    QVERIFY(addReturn(f.returns, supplierId, 5000, purchaseId, when) > 0);

    const data::SupplierReport report = f.service.summary();
    QCOMPARE(report.totalSuppliers, 1);
    QCOMPARE(report.grandTotalOwedCents, 25000LL);

    const data::SupplierSummary& row = report.suppliers.first();
    QCOMPARE(row.openingBalanceCents, 0LL);
    QCOMPARE(row.purchasesTotalCents, 50000LL);
    QCOMPARE(row.paymentsTotalCents, 20000LL);
    QCOMPARE(row.returnsTotalCents, 5000LL);
    // 0 + 50000 - 20000 - 5000
    QCOMPARE(row.currentBalanceCents, 25000LL);
    QCOMPARE(row.purchaseCount, 1);
}

void SupplierReportTest::summary_multiple_suppliers()
{
    Fixture f(m_dir.filePath(QStringLiteral("summary_multiple.sqlite")));

    // One the shop owes, one it is ahead on, and one that squares off exactly.
    const int owing = addSupplier(f.suppliers, QStringLiteral("دائن للمحل"), 40000);
    const int owed = addSupplier(f.suppliers, QStringLiteral("له للمحل"), 40000);
    const int clear = addSupplier(f.suppliers, QStringLiteral("مسوّى"), 10000);
    QVERIFY(owing > 0);
    QVERIFY(owed > 0);
    QVERIFY(clear > 0);

    QVERIFY(addPayment(f.payments, owed, 40000, std::nullopt, pastIso(5)) > 0);
    QVERIFY(addPayment(f.payments, clear, 10000, std::nullopt, pastIso(5)) > 0);

    const data::SupplierReport report = f.service.summary();
    QCOMPARE(report.totalSuppliers, 3);

    // Only the 40000 the shop owes is counted. The -40000 and the 0 are left out,
    // because netting them would produce a total matching no row on the list.
    QCOMPARE(report.grandTotalOwedCents, 40000LL);

    long long check = 0;
    int positive = 0;
    for (const data::SupplierSummary& row : report.suppliers) {
        if (row.currentBalanceCents > 0) {
            check += row.currentBalanceCents;
            ++positive;
        }
    }
    QCOMPARE(positive, 1);
    QCOMPARE(check, report.grandTotalOwedCents);
}

void SupplierReportTest::summary_unpaid_invoice_count()
{
    Fixture f(m_dir.filePath(QStringLiteral("summary_unpaid.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد فواتير"), 0);
    QVERIFY(supplierId > 0);

    const QString when = pastIso(10);
    const int unpaidA = addPurchase(f.purchases, supplierId, 30000, when);
    const int unpaidB = addPurchase(f.purchases, supplierId, 20000, when);
    const int paid = addPurchase(f.purchases, supplierId, 10000, when);
    QVERIFY(unpaidA > 0);
    QVERIFY(unpaidB > 0);
    QVERIFY(paid > 0);
    QVERIFY(addPayment(f.payments, supplierId, 10000, paid, when) > 0);

    const data::SupplierReport report = f.service.summary();
    const data::SupplierSummary& row = report.suppliers.first();
    QCOMPARE(row.purchaseCount, 3);
    // The settled invoice is left out, and a part payment that does not clear
    // its invoice still counts.
    QCOMPARE(row.unpaidInvoiceCount, 2);
    QCOMPARE(row.unpaidTotalCents, 50000LL);
    QCOMPARE(row.currentBalanceCents, 50000LL);
}

void SupplierReportTest::summary_ignores_inactive_supplier()
{
    Fixture f(m_dir.filePath(QStringLiteral("summary_inactive.sqlite")));
    const int activeId = addSupplier(f.suppliers, QStringLiteral("نشط"), 10000);
    const int retiredId = addSupplier(f.suppliers, QStringLiteral("موقوف"), 70000);
    QVERIFY(activeId > 0);
    QVERIFY(retiredId > 0);
    QVERIFY(f.suppliers.setActive(retiredId, false));

    const data::SupplierReport report = f.service.summary();
    // A retired supplier is not being dealt with, and its old balance has no
    // place in a total of what is owed today.
    QCOMPARE(report.totalSuppliers, 1);
    QCOMPARE(report.grandTotalOwedCents, 10000LL);
    QCOMPARE(report.suppliers.first().supplier.id, activeId);
}

void SupplierReportTest::period_report_with_purchases_and_payments()
{
    Fixture f(m_dir.filePath(QStringLiteral("period_split.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد فترة"), 0);
    QVERIFY(supplierId > 0);

    // One purchase in March, one payment in April, both a month back so the dates
    // are in the past no matter when this runs.
    const QDateTime march = QDateTime::currentDateTime().addMonths(-2);
    const QDateTime april = QDateTime::currentDateTime().addMonths(-1);
    QVERIFY(addPurchase(f.purchases, supplierId, 50000, data::toIso(march)) > 0);
    QVERIFY(addPayment(f.payments, supplierId, 20000, std::nullopt, data::toIso(april)) > 0);

    const QString marchFrom = march.date().toString(Qt::ISODate) + QStringLiteral("T00:00:00.000");
    const QString marchTo = march.date().addDays(march.date().daysInMonth() - 1).toString(Qt::ISODate)
                            + QStringLiteral("T23:59:59.999");
    const QString aprilFrom = april.date().toString(Qt::ISODate) + QStringLiteral("T00:00:00.000");
    const QString aprilTo = april.date().addDays(april.date().daysInMonth() - 1).toString(Qt::ISODate)
                            + QStringLiteral("T23:59:59.999");

    const data::SupplierPeriodReport inMarch = f.service.periodReport(supplierId, marchFrom, marchTo);
    QCOMPARE(inMarch.supplierId, supplierId);
    QCOMPARE(inMarch.purchasesCents, 50000LL);
    // The payment is in April, so March must not see it.
    QCOMPARE(inMarch.paymentsCents, 0LL);
    QCOMPARE(inMarch.netChangeCents, 50000LL);
    QCOMPARE(inMarch.purchases.size(), 1);

    const data::SupplierPeriodReport inApril = f.service.periodReport(supplierId, aprilFrom, aprilTo);
    QCOMPARE(inApril.purchasesCents, 0LL);
    QCOMPARE(inApril.paymentsCents, 20000LL);
    QCOMPARE(inApril.netChangeCents, -20000LL);

    // The same month asked for by plain dates, which is how an operator would
    // type it, has to give the same answer. The upper bound has to reach the last
    // moment of the day or a transaction on the final day drops out.
    const QString marchDay = march.date().toString(Qt::ISODate);
    const QString lastDayOfMarch = march.date().addDays(march.date().daysInMonth() - 1).toString(Qt::ISODate);
    const data::SupplierPeriodReport byPlainDates =
        f.service.periodReport(supplierId, marchDay, lastDayOfMarch);
    QCOMPARE(byPlainDates.purchasesCents, 50000LL);
    QCOMPARE(byPlainDates.purchases.size(), 1);
}

void SupplierReportTest::period_report_returns_and_net_change()
{
    Fixture f(m_dir.filePath(QStringLiteral("period_returns.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد مرتجع"), 0);
    QVERIFY(supplierId > 0);

    const QDateTime when = QDateTime::currentDateTime().addDays(-20);
    const QString at = data::toIso(when);
    const int purchaseId = addPurchase(f.purchases, supplierId, 40000, at);
    QVERIFY(purchaseId > 0);
    QVERIFY(addPayment(f.payments, supplierId, 10000, purchaseId, at) > 0);
    QVERIFY(addReturn(f.returns, supplierId, 4000, purchaseId, at) > 0);

    // A return from outside the window, which must not be counted with them.
    QVERIFY(addReturn(f.returns, supplierId, 999999, std::nullopt, pastIso(90)) > 0);

    const QString from = when.date().toString(Qt::ISODate) + QStringLiteral("T00:00:00.000");
    const QString to = when.date().toString(Qt::ISODate) + QStringLiteral("T23:59:59.999");
    const data::SupplierPeriodReport report = f.service.periodReport(supplierId, from, to);

    QCOMPARE(report.purchasesCents, 40000LL);
    QCOMPARE(report.paymentsCents, 10000LL);
    QCOMPARE(report.returnsCents, 4000LL);
    // 40000 - 10000 - 4000
    QCOMPARE(report.netChangeCents, 26000LL);
    QCOMPARE(report.returns.size(), 1);

    // The summary reads the whole history and so does see the old return, which
    // is the point of having both readings: the period answers "what did this
    // month do", the summary answers "where do we stand". 40000 - 10000 - 4000
    // - 999999 leaves the shop far ahead of the supplier, and a negative balance
    // is not owed, so the grand total is empty rather than negative.
    const data::SupplierReport summary = f.service.summary();
    QCOMPARE(summary.suppliers.first().returnsTotalCents, 4000LL + 999999LL);
    QCOMPARE(summary.suppliers.first().currentBalanceCents, 40000LL - 10000LL - 4000LL - 999999LL);
    QCOMPARE(summary.grandTotalOwedCents, 0LL);
}

void SupplierReportTest::period_report_unknown_supplier_is_empty()
{
    Fixture f(m_dir.filePath(QStringLiteral("period_unknown.sqlite")));

    const data::SupplierPeriodReport report = f.service.periodReport(4242, pastIso(1), pastIso(0));
    // Zeroed rather than filled, so a wrong id cannot be mistaken for a quiet
    // period.
    QCOMPARE(report.supplierId, 4242);
    QCOMPARE(report.purchasesCents, 0LL);
    QCOMPARE(report.paymentsCents, 0LL);
    QCOMPARE(report.returnsCents, 0LL);
    QCOMPARE(report.netChangeCents, 0LL);
    QVERIFY(report.purchases.isEmpty());
    QVERIFY(report.payments.isEmpty());
    QVERIFY(report.returns.isEmpty());
}

QTEST_MAIN(SupplierReportTest)
#include "tst_supplier_report.moc"
