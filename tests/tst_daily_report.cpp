#include <QtTest/QtTest>

#include <QTemporaryDir>

#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/customer_transaction_repository.h"
#include "data/daily_report_service.h"
#include "data/database.h"
#include "data/expense_repository.h"
#include "data/owner_drawing_repository.h"
#include "data/product_repository.h"
#include "data/sale_item_repository.h"
#include "data/sale_repository.h"

using namespace app;

namespace {

QString todayIso()
{
    return QDateTime::currentDateTime().date().toString(Qt::ISODate);
}

QString yesterdayIso()
{
    return QDateTime::currentDateTime().addDays(-1).date().toString(Qt::ISODate);
}

QString atNoon(const QString& dayIso)
{
    return dayIso + QStringLiteral("T12:00:00.000");
}

struct Fixture {
    explicit Fixture(const QString& path)
        : db(path)
        , sales(db)
        , expenses(db)
        , drawings(db)
        , customerTx(db)
        , cashSessions(db)
        , service(db, sales, expenses, drawings, customerTx, cashSessions)
    {
    }

    data::Database db;
    data::SaleRepository sales;
    data::ExpenseRepository expenses;
    data::OwnerDrawingRepository drawings;
    data::CustomerTransactionRepository customerTx;
    data::CashSessionRepository cashSessions;
    data::DailyReportService service;
};

int addProduct(data::ProductRepository& products, const QString& name, long long costPriceCents,
               long long salePriceCents, const QString& barcode = QString())
{
    core::Product product;
    product.name = name;
    // An empty barcode is stored as NULL, and UNIQUE lets any number of NULLs
    // through, so a fixture may create as many products as it needs. The ones
    // that pass a barcode still get their own distinct one.
    product.barcode = barcode;
    product.costPriceCents = costPriceCents;
    product.salePriceCents = salePriceCents;
    return products.save(product);
}

// A sale written through the repositories rather than through SaleService, so the
// moment it happened can be chosen. SaleService always stamps now, which would
// make a test of any day but today impossible to write.
int addSale(data::SaleRepository& sales,
            data::SaleItemRepository& items,
            long long totalCents,
            const QString& createdAt)
{
    core::Sale sale;
    sale.createdAt = QDateTime::fromString(createdAt, Qt::ISODateWithMs);
    sale.totalCents = totalCents;
    sale.deviceId = QStringLiteral("PHONE_01");
    return sales.insert(sale);
}

void addLine(data::SaleItemRepository& items, int saleId, int productId, long long quantity,
             long long unitPriceCents, long long unitCostCents)
{
    core::SaleItem item;
    item.saleId = saleId;
    item.productId = productId;
    item.quantity = quantity;
    item.unitPriceCents = unitPriceCents;
    item.unitCostCents = unitCostCents;
    QVERIFY(items.insert(item) > 0);
}

int addCustomer(data::CustomerRepository& customers, const QString& name)
{
    core::Customer customer;
    customer.name = name;
    return customers.save(customer);
}

} // namespace

class DailyReportTest : public QObject
{
    Q_OBJECT

private slots:
    void daily_report_empty();
    void daily_report_simple_sale();
    void daily_report_top_products();
    void daily_report_new_debts();
    void daily_report_ignores_other_days();
    void daily_report_includes_expenses();
    void daily_report_includes_drawings();
    void daily_report_closed_session_cash();
    void daily_report_open_session_has_no_closing();

private:
    QTemporaryDir m_dir;
};

void DailyReportTest::daily_report_empty()
{
    Fixture f(m_dir.filePath(QStringLiteral("empty.sqlite")));

    const data::DailyReport report = f.service.forDay(todayIso());
    QCOMPARE(report.dayIso, todayIso());
    // A day with no trading has a report, not a missing one, so a caller reading
    // it gets zeroes rather than having to test each field for being unset.
    QCOMPARE(report.salesTotalCents, 0LL);
    QCOMPARE(report.cogsTotalCents, 0LL);
    QCOMPARE(report.grossProfitCents, 0LL);
    QCOMPARE(report.invoiceCount, 0);
    QCOMPARE(report.expensesCents, 0LL);
    QCOMPARE(report.drawingsCents, 0LL);
    QCOMPARE(report.netProfitCents, 0LL);
    QCOMPARE(report.cashOpeningCents, 0LL);
    QCOMPARE(report.cashClosingCents, 0LL);
    QCOMPARE(report.cashExpectedCents, 0LL);
    QCOMPARE(report.cashActualCents, 0LL);
    QCOMPARE(report.cashDifferenceCents, 0LL);
    QVERIFY(report.topProducts.isEmpty());
    QVERIFY(report.newDebts.isEmpty());
}

void DailyReportTest::daily_report_simple_sale()
{
    Fixture f(m_dir.filePath(QStringLiteral("simple_sale.sqlite")));
    data::ProductRepository products(f.db);
    data::SaleItemRepository items(f.db);

    const int productId = addProduct(products, QStringLiteral("شاي"), 3000, 5000, QStringLiteral("T-1"));
    QVERIFY(productId > 0);

    // Two units at 5000 is 10000 of takings against 6000 of cost, frozen on the
    // line at the moment of sale.
    const QString at = atNoon(todayIso());
    const int saleId = addSale(f.sales, items, 10000, at);
    QVERIFY(saleId > 0);
    addLine(items, saleId, productId, 2, 5000, 3000);

    const data::DailyReport report = f.service.forDay(todayIso());
    QCOMPARE(report.salesTotalCents, 10000LL);
    QCOMPARE(report.cogsTotalCents, 6000LL);
    QCOMPARE(report.grossProfitCents, 4000LL);
    QCOMPARE(report.invoiceCount, 1);
    QCOMPARE(report.netProfitCents, 4000LL);
    QCOMPARE(report.topProducts.size(), 1);
    QCOMPARE(report.topProducts.first().quantitySold, 2LL);
    QCOMPARE(report.topProducts.first().revenueCents, 10000LL);
    QCOMPARE(report.topProducts.first().productName, QStringLiteral("شاي"));
}

void DailyReportTest::daily_report_top_products()
{
    Fixture f(m_dir.filePath(QStringLiteral("top_products.sqlite")));
    data::ProductRepository products(f.db);
    data::SaleItemRepository items(f.db);

    const int tea = addProduct(products, QStringLiteral("شاي"), 1000, 2000, QStringLiteral("T-T"));
    const int coffee = addProduct(products, QStringLiteral("قهوة"), 2000, 3000, QStringLiteral("T-C"));
    const int sugar = addProduct(products, QStringLiteral("سكر"), 500, 900, QStringLiteral("T-S"));
    QVERIFY(tea > 0);
    QVERIFY(coffee > 0);
    QVERIFY(sugar > 0);

    const QString at = atNoon(todayIso());
    // 2*2000 + 3*3000 + 10*900
    const int saleId = addSale(f.sales, items, 22000, at);
    QVERIFY(saleId > 0);
    // Sugar moves the most units, so it leads even though it is the cheapest line.
    addLine(items, saleId, tea, 2, 2000, 1000);
    addLine(items, saleId, coffee, 3, 3000, 2000);
    addLine(items, saleId, sugar, 10, 900, 500);

    const data::DailyReport report = f.service.forDay(todayIso());
    QCOMPARE(report.salesTotalCents, 22000LL);
    QCOMPARE(report.topProducts.size(), 3);
    QCOMPARE(report.topProducts.first().productId, sugar);
    QCOMPARE(report.topProducts.first().quantitySold, 10LL);
    QCOMPARE(report.topProducts.at(1).productId, coffee);
    QCOMPARE(report.topProducts.at(2).productId, tea);
    // Line revenue is the quantity times the price the line was sold at; there is
    // no stored line total to read it from.
    QCOMPARE(report.topProducts.first().revenueCents, 9000LL);
    QCOMPARE(report.topProducts.at(1).revenueCents, 9000LL);
    QCOMPARE(report.topProducts.last().revenueCents, 4000LL);
}

void DailyReportTest::daily_report_new_debts()
{
    Fixture f(m_dir.filePath(QStringLiteral("new_debts.sqlite")));
    data::CustomerRepository customers(f.db);

    const int ahmad = addCustomer(customers, QStringLiteral("أحمد"));
    const int sara = addCustomer(customers, QStringLiteral("سارة"));
    QVERIFY(ahmad > 0);
    QVERIFY(sara > 0);

    core::CustomerTransaction debtA;
    debtA.customerId = ahmad;
    debtA.amountCents = 5000;
    debtA.createdAt = QDateTime::fromString(atNoon(todayIso()), Qt::ISODateWithMs);
    QVERIFY(f.customerTx.insert(debtA) > 0);

    core::CustomerTransaction debtB;
    debtB.customerId = sara;
    debtB.amountCents = 2000;
    debtB.createdAt = QDateTime::fromString(atNoon(todayIso()), Qt::ISODateWithMs);
    QVERIFY(f.customerTx.insert(debtB) > 0);

    // A reversal is a negative row against the original, which is what
    // reversed_transaction_id marks it with. It has to show up on its own: if the
    // report kept only the positive rows, the cancelled sale would still be
    // listed as a debt taken today and nothing on the day would say it was
    // undone.
    core::CustomerTransaction reversal;
    reversal.customerId = ahmad;
    reversal.amountCents = -5000;
    reversal.createdAt = QDateTime::fromString(atNoon(todayIso()), Qt::ISODateWithMs);
    QVERIFY(f.customerTx.insert(reversal) > 0);

    const data::DailyReport report = f.service.forDay(todayIso());
    QCOMPARE(report.newDebts.size(), 3);

    // Signed as written, so the day's rows add up to what was actually kept.
    long long total = 0;
    int negativeRows = 0;
    for (const data::NewDebt& debt : report.newDebts) {
        total += debt.amountCents;
        if (debt.amountCents < 0) {
            ++negativeRows;
        }
    }
    QCOMPARE(negativeRows, 1);
    QCOMPARE(total, 2000LL);
}

void DailyReportTest::daily_report_ignores_other_days()
{
    Fixture f(m_dir.filePath(QStringLiteral("other_days.sqlite")));
    data::ProductRepository products(f.db);
    data::SaleItemRepository items(f.db);

    const int productId = addProduct(products, QStringLiteral("منتج"), 1000, 2000, QStringLiteral("T-1"));
    QVERIFY(productId > 0);

    const int yesterdayId = addSale(f.sales, items, 7000, atNoon(yesterdayIso()));
    QVERIFY(yesterdayId > 0);
    addLine(items, yesterdayId, productId, 1, 2000, 1000);

    const int todayId = addSale(f.sales, items, 3000, atNoon(todayIso()));
    QVERIFY(todayId > 0);
    addLine(items, todayId, productId, 1, 2000, 1000);

    // Late on the same day, the case a bare upper bound used to drop.
    const int lateId = addSale(f.sales, items, 5000, todayIso() + QStringLiteral("T23:30:00.000"));
    QVERIFY(lateId > 0);
    addLine(items, lateId, productId, 1, 2000, 1000);

    const data::DailyReport today = f.service.forDay(todayIso());
    QCOMPARE(today.invoiceCount, 2);
    QCOMPARE(today.salesTotalCents, 8000LL);
    QCOMPARE(today.cogsTotalCents, 2000LL);
    QCOMPARE(today.grossProfitCents, 6000LL);

    const data::DailyReport yesterday = f.service.forDay(yesterdayIso());
    QCOMPARE(yesterday.invoiceCount, 1);
    QCOMPARE(yesterday.salesTotalCents, 7000LL);
}

void DailyReportTest::daily_report_includes_expenses()
{
    Fixture f(m_dir.filePath(QStringLiteral("expenses.sqlite")));
    data::ProductRepository products(f.db);
    data::SaleItemRepository items(f.db);

    const int productId = addProduct(products, QStringLiteral("بضاعة"), 3000, 5000, QStringLiteral("T-1"));
    QVERIFY(productId > 0);
    const int saleId = addSale(f.sales, items, 10000, atNoon(todayIso()));
    QVERIFY(saleId > 0);
    addLine(items, saleId, productId, 2, 5000, 3000);

    core::Expense expense;
    expense.amountCents = 1500;
    expense.label = QStringLiteral("إيجار");
    expense.createdAt = QDateTime::fromString(atNoon(todayIso()), Qt::ISODateWithMs);
    QVERIFY(f.expenses.insert(expense) > 0);

    // An expense from yesterday must not land on today's report.
    core::Expense yesterdayExpense;
    yesterdayExpense.amountCents = 999999;
    yesterdayExpense.createdAt = QDateTime::fromString(atNoon(yesterdayIso()), Qt::ISODateWithMs);
    QVERIFY(f.expenses.insert(yesterdayExpense) > 0);

    const data::DailyReport report = f.service.forDay(todayIso());
    QCOMPARE(report.grossProfitCents, 4000LL);
    QCOMPARE(report.expensesCents, 1500LL);
    QCOMPARE(report.netProfitCents, 2500LL);
}

void DailyReportTest::daily_report_includes_drawings()
{
    Fixture f(m_dir.filePath(QStringLiteral("drawings.sqlite")));

    core::OwnerDrawing drawing;
    drawing.amountCents = 3000;
    drawing.createdAt = QDateTime::fromString(atNoon(todayIso()), Qt::ISODateWithMs);
    QVERIFY(f.drawings.insert(drawing) > 0);

    const data::DailyReport report = f.service.forDay(todayIso());
    QCOMPARE(report.drawingsCents, 3000LL);
    // Money the owner took out is not profit, even though it was never an expense
    // either, so it comes off the net.
    QCOMPARE(report.netProfitCents, -3000LL);
}

void DailyReportTest::daily_report_closed_session_cash()
{
    Fixture f(m_dir.filePath(QStringLiteral("closed_session.sqlite")));

    QVERIFY(f.cashSessions.open(100000) > 0);
    const auto session = f.cashSessions.findOpen();
    QVERIFY(session.has_value());
    QVERIFY(f.cashSessions.close(session->id, 95000, 100000, -5000));

    const data::DailyReport report = f.service.forDay(todayIso());
    QCOMPARE(report.cashOpeningCents, 100000LL);
    QCOMPARE(report.cashClosingCents, 95000LL);
    QCOMPARE(report.cashExpectedCents, 100000LL);
    QCOMPARE(report.cashActualCents, 95000LL);
    // Counted 5000 less than expected, kept as a negative so a shortfall reads as
    // one.
    QCOMPARE(report.cashDifferenceCents, -5000LL);
}

void DailyReportTest::daily_report_open_session_has_no_closing()
{
    Fixture f(m_dir.filePath(QStringLiteral("open_session.sqlite")));

    QVERIFY(f.cashSessions.open(20000) > 0);

    const data::DailyReport report = f.service.forDay(todayIso());
    QCOMPARE(report.cashOpeningCents, 20000LL);
    // A session still open has no counted figure. Reporting zero here would be a
    // claim the till came out empty, which is a different and much worse thing to
    // say than "not counted yet".
    QCOMPARE(report.cashClosingCents, 0LL);
    QCOMPARE(report.cashExpectedCents, 0LL);
    QCOMPARE(report.cashActualCents, 0LL);
    QCOMPARE(report.cashDifferenceCents, 0LL);

    // A day with no session at all is not the same as one with an open session,
    // and the report cannot tell them apart on the opening figure alone.
    QCOMPARE(f.service.forDay(yesterdayIso()).cashOpeningCents, 0LL);
}

QTEST_MAIN(DailyReportTest)
#include "tst_daily_report.moc"
