#include <QtTest/QtTest>

#include "core/cash_session_calculator.h"
#include "core/profit_loss_calculator.h"
#include "core/zakat_calculator.h"

class FinanceTest : public QObject
{
    Q_OBJECT

private slots:
    void profitLoss();
    void cashSessionVariance();
    void zakatBaseTreatsDebtAsCash();
    void zakatBaseStockCashReceivables();
};

void FinanceTest::profitLoss()
{
    using app::core::ProfitLossCalculator;

    const app::core::ProfitLossReport report =
        ProfitLossCalculator::compute(/*revenue=*/100000,
                                      /*cogs=*/60000,
                                      /*expenses=*/15000,
                                      /*drawings=*/5000);

    QCOMPARE(report.revenueCents, 100000);
    QCOMPARE(report.cogsCents, 60000);
    QCOMPARE(report.grossProfitCents, 40000);
    QCOMPARE(report.expensesCents, 15000);
    QCOMPARE(report.netProfitCents, 25000);
    QCOMPARE(report.drawingsCents, 5000);
}

void FinanceTest::cashSessionVariance()
{
    using app::core::CashSessionCalculator;

    const long long expected = CashSessionCalculator::expectedTotalCents(20000, 15000 + (-4000));
    QCOMPARE(expected, 31000);
    QCOMPARE(CashSessionCalculator::varianceCents(31500, expected), 500);
    QCOMPARE(CashSessionCalculator::varianceCents(30000, expected), -1000);
}

void FinanceTest::zakatBaseTreatsDebtAsCash()
{
    using app::core::ZakatCalculator;

    QCOMPARE(ZakatCalculator::zakatBaseCents(100000, 50000), 150000);
    QCOMPARE(ZakatCalculator::zakatBaseCents(0, 70000), 70000);
}

void FinanceTest::zakatBaseStockCashReceivables()
{
    using app::core::ZakatCalculator;

    app::core::ZakatInputs inputs;
    inputs.stockValueCents = 100000;
    inputs.cashCents = 50000;
    inputs.receivablesCents = 30000;
    QCOMPARE(ZakatCalculator::zakatBase(inputs), 180000LL);

    // Each leg on its own is enough to make the base, and a shop holding stock
    // with an empty till is not a shop that owes no zakat.
    app::core::ZakatInputs stockOnly;
    stockOnly.stockValueCents = 40000;
    QCOMPARE(ZakatCalculator::zakatBase(stockOnly), 40000LL);

    app::core::ZakatInputs nothing;
    QCOMPARE(ZakatCalculator::zakatBase(nothing), 0LL);
}

QTEST_GUILESS_MAIN(FinanceTest)
#include "tst_finance.moc"
