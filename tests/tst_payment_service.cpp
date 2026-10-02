#include <QtTest/QtTest>

#include <QTemporaryDir>

#include "core/cash_movement.h"
#include "core/cash_session_calculator.h"
#include "core/supplier_payment.h"
#include "data/cash_session_repository.h"
#include "data/cash_movement_repository.h"
#include "data/database.h"
#include "data/date_utils.h"
#include "data/purchase_repository.h"
#include "data/supplier_payment_repository.h"
#include "data/supplier_payment_service.h"
#include "data/supplier_repository.h"

using namespace app;

namespace {

// One database per test: each case counts the payments it wrote, so a shared
// file would let the order the tests run in decide what each one sees.
struct Fixture {
    explicit Fixture(const QString& path)
        : db(path)
        , payments(db)
        , suppliers(db)
        , purchases(db)
        , service(db, payments, suppliers, purchases)
    {
        // Every fixture opens a till. These cases are about the supplier's
        // balance, but they pay cash — the method the form leads with, and the
        // one that has to name a session so the money leaving the drawer is
        // counted. Nothing below is about the drawer itself; that is what
        // tst_purchase_service's cash tests and the supplier-cash cases here
        // cover.
        sessionId = data::CashSessionRepository(db).open(100000);
    }

    data::Database db;
    data::SupplierPaymentRepository payments;
    data::SupplierRepository suppliers;
    data::PurchaseRepository purchases;
    data::SupplierPaymentService service;
    int sessionId = 0;
};

int addSupplier(data::SupplierRepository& suppliers, const QString& name, long long openingBalanceCents = 0)
{
    core::Supplier supplier;
    supplier.name = name;
    supplier.openingBalanceCents = openingBalanceCents;
    return suppliers.save(supplier);
}

int addPurchase(data::PurchaseRepository& purchases, int supplierId, long long totalCents,
                const QString& purchasedAt = QString())
{
    core::Purchase purchase;
    purchase.supplierId = supplierId;
    purchase.invoiceNumber = QStringLiteral("");
    purchase.note = QStringLiteral("");
    purchase.purchasedAt = purchasedAt.isEmpty() ? data::nowIso() : purchasedAt;
    purchase.totalCents = totalCents;
    purchase.subtotalCents = totalCents;
    purchase.createdAt = data::nowIso();
    return purchases.insert(purchase);
}

QString isoAt(int day)
{
    return QStringLiteral("2026-03-%1T08:00:00.000").arg(day, 2, 10, QLatin1Char('0'));
}

} // namespace

class SupplierPaymentServiceTest : public QObject {
    Q_OBJECT

private slots:
    void record_payment_simple();
    void record_payment_linked_to_purchase();
    void payment_above_invoice_remaining_fails();
    void record_payment_above_balance_allowed();
    void record_payment_rejects_zero();
    void record_payment_rejects_negative();
    void record_payment_rejects_wrong_supplier();
    void unpaid_invoices_excludes_fully_paid();
    void unpaid_invoices_ordered_by_date();
    void balance_matches_payments();
    void cash_payment_lowers_the_expected_till();
    void cash_payment_without_a_session_is_refused();
    void cash_payment_refuses_a_closed_session();
    void credit_and_bank_touch_no_cash();
    void method_round_trips_through_the_column();
};

void SupplierPaymentServiceTest::record_payment_simple()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("record_payment_simple.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);

    // No purchaseId: a general payment, taken off the balance as a whole. With
    // no invoice and no opening debt there is nothing owed, so paying puts the
    // supplier in credit by exactly the amount paid.
    const data::SupplierPaymentResult result =
        f.service.recordPayment(supplierId, std::nullopt, 5000, data::nowIso(), QStringLiteral("test"), core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.paymentId > 0);

    const auto stored = f.payments.findById(result.paymentId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->supplierId, supplierId);
    QCOMPARE(stored->amountCents, 5000LL);
    QVERIFY(!stored->purchaseId.has_value());
    QCOMPARE(stored->note, QStringLiteral("test"));

    QCOMPARE(f.service.balanceFor(supplierId), -5000LL);
}

void SupplierPaymentServiceTest::record_payment_linked_to_purchase()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("record_payment_linked_to_purchase.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد فاتورة"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(f.purchases, supplierId, 50000);
    QVERIFY(purchaseId > 0);
    QCOMPARE(f.service.balanceFor(supplierId), 50000LL);

    const data::SupplierPaymentResult result =
        f.service.recordPayment(supplierId, purchaseId, 20000, data::nowIso(), QStringLiteral("دفعة"), core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));

    const auto stored = f.payments.findById(result.paymentId);
    QVERIFY(stored.has_value());
    QVERIFY(stored->purchaseId.has_value());
    QCOMPARE(*stored->purchaseId, purchaseId);

    // 50000 invoiced less the 20000 paid: the invoice is still partly owed, and
    // the same payment is what makes it show up as partly settled.
    QCOMPARE(f.service.balanceFor(supplierId), 30000LL);

    const auto unpaid = f.service.unpaidInvoicesFor(supplierId);
    QCOMPARE(unpaid.size(), 1);
    QCOMPARE(unpaid[0].purchase.id, purchaseId);
    QCOMPARE(unpaid[0].remainingCents, 30000LL);
}

void SupplierPaymentServiceTest::payment_above_invoice_remaining_fails()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("payment_above_invoice_remaining_fails.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(f.purchases, supplierId, 50000);
    QVERIFY(purchaseId > 0);
    QCOMPARE(f.service.balanceFor(supplierId), 50000LL);

    // 50000 is what the invoice is worth, so 50001 against it is not a payment:
    // it would leave a credit the balance then reports as a debt.
    const data::SupplierPaymentResult result =
        f.service.recordPayment(supplierId, purchaseId, 50000LL + 1LL, data::nowIso(), QString(), core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(f.service.balanceFor(supplierId), 50000LL);
    QCOMPARE(f.payments.findBySupplierId(supplierId).size(), 0);
}

void SupplierPaymentServiceTest::record_payment_above_balance_allowed()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("record_payment_above_balance_allowed.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(f.purchases, supplierId, 50000);
    QVERIFY(purchaseId > 0);

    // The invoice is paid, so the balance is back to nothing owed.
    const data::SupplierPaymentResult settled =
        f.service.recordPayment(supplierId, purchaseId, 50000, data::nowIso(), QString(), core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(settled.ok, qPrintable(settled.error));
    QCOMPARE(f.service.balanceFor(supplierId), 0LL);

    // Money handed over on top of that, with no invoice named, is an advance:
    // the shop paid for goods before they arrived, so the supplier is in credit
    // and the balance says so. A general payment is not capped at the balance.
    const data::SupplierPaymentResult advance =
        f.service.recordPayment(supplierId, std::nullopt, 20000, data::nowIso(), QString(), core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(advance.ok, qPrintable(advance.error));
    QCOMPARE(f.service.balanceFor(supplierId), -20000LL);
    QCOMPARE(f.service.unpaidInvoicesFor(supplierId).size(), 0);
}

void SupplierPaymentServiceTest::record_payment_rejects_zero()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("record_payment_rejects_zero.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);

    const data::SupplierPaymentResult result =
        f.service.recordPayment(supplierId, std::nullopt, 0, data::nowIso(), QString(), core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("amount must be positive"));
    QCOMPARE(f.payments.findBySupplierId(supplierId).size(), std::size_t(0));
}

void SupplierPaymentServiceTest::record_payment_rejects_negative()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("record_payment_rejects_negative.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);

    // A negative payment would be read as money owed to the supplier rather than
    // paid to them, so it is refused instead of quietly reversing the sign.
    const data::SupplierPaymentResult result =
        f.service.recordPayment(supplierId, std::nullopt, -100, data::nowIso(), QString(), core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("amount must be positive"));
    QCOMPARE(f.payments.findBySupplierId(supplierId).size(), std::size_t(0));
    QCOMPARE(f.service.balanceFor(supplierId), 0LL);
}

void SupplierPaymentServiceTest::record_payment_rejects_wrong_supplier()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("record_payment_rejects_wrong_supplier.sqlite")));
    const int supplierA = addSupplier(f.suppliers, QStringLiteral("مورد أ"));
    QVERIFY(supplierA > 0);
    const int supplierB = addSupplier(f.suppliers, QStringLiteral("مورد ب"));
    QVERIFY(supplierB > 0);
    const int purchaseId = addPurchase(f.purchases, supplierA, 50000);
    QVERIFY(purchaseId > 0);

    // Paying B against A's invoice would reduce B's balance with money that
    // settled A's debt, so the two have to agree before anything is written.
    const data::SupplierPaymentResult result =
        f.service.recordPayment(supplierB, purchaseId, 100, data::nowIso(), QString(), core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("purchase does not belong to supplier"));

    QCOMPARE(f.payments.findBySupplierId(supplierB).size(), std::size_t(0));
    QCOMPARE(f.service.balanceFor(supplierA), 50000LL);
    QCOMPARE(f.service.balanceFor(supplierB), 0LL);
}

void SupplierPaymentServiceTest::unpaid_invoices_excludes_fully_paid()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("unpaid_invoices_excludes_fully_paid.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int paidId = addPurchase(f.purchases, supplierId, 30000, isoAt(1));
    QVERIFY(paidId > 0);
    const int unpaidId = addPurchase(f.purchases, supplierId, 20000, isoAt(2));
    QVERIFY(unpaidId > 0);

    const data::SupplierPaymentResult result =
        f.service.recordPayment(supplierId, paidId, 30000, data::nowIso(), QString(), core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));

    // The settled invoice drops out, the other one stays, and the two together
    // leave only the unpaid one owed.
    const auto unpaid = f.service.unpaidInvoicesFor(supplierId);
    QCOMPARE(unpaid.size(), 1);
    QCOMPARE(unpaid[0].purchase.id, unpaidId);
    QCOMPARE(unpaid[0].remainingCents, 20000LL);
    QCOMPARE(f.service.balanceFor(supplierId), 20000LL);
}

void SupplierPaymentServiceTest::unpaid_invoices_ordered_by_date()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("unpaid_invoices_ordered_by_date.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);

    // Inserted newest first, so an ordering by id would come out the other way
    // round and the assertion below would be a real check.
    const int thirdId = addPurchase(f.purchases, supplierId, 3000, isoAt(3));
    const int firstId = addPurchase(f.purchases, supplierId, 1000, isoAt(1));
    const int secondId = addPurchase(f.purchases, supplierId, 2000, isoAt(2));
    QVERIFY(thirdId > 0);
    QVERIFY(firstId > 0);
    QVERIFY(secondId > 0);

    const auto unpaid = f.service.unpaidInvoicesFor(supplierId);
    QCOMPARE(unpaid.size(), 3);
    QCOMPARE(unpaid[0].purchase.id, firstId);
    QCOMPARE(unpaid[1].purchase.id, secondId);
    QCOMPARE(unpaid[2].purchase.id, thirdId);
    QCOMPARE(unpaid[0].remainingCents, 1000LL);
    QCOMPARE(unpaid[2].remainingCents, 3000LL);
}

void SupplierPaymentServiceTest::balance_matches_payments()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("balance_matches_payments.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(f.purchases, supplierId, 40000, isoAt(1));
    QVERIFY(purchaseId > 0);

    // Two against the invoice, two general: all four come off the balance the
    // same way, which is the point of a general payment not needing an invoice.
    const std::vector<std::pair<std::optional<int>, long long>> payments = {
        {purchaseId, 10000},
        {purchaseId, 5000},
        {std::nullopt, 2000},
        {std::nullopt, 500},
    };
    for (const auto& [target, amountCents] : payments) {
        const data::SupplierPaymentResult result =
            f.service.recordPayment(supplierId, target, amountCents, data::nowIso(), QString(), core::SupplierPaymentMethod::Cash, f.sessionId);
        QVERIFY2(result.ok, qPrintable(result.error));
    }

    // 40000 invoiced, 17500 paid: the invoice is still owed and the balance says
    // so, whether the payments named it or not.
    QCOMPARE(f.service.balanceFor(supplierId), 22500LL);
    QCOMPARE(f.payments.findBySupplierId(supplierId).size(), std::size_t(4));
    QCOMPARE(f.payments.findByPurchaseId(purchaseId).size(), std::size_t(2));

    const auto unpaid = f.service.unpaidInvoicesFor(supplierId);
    QCOMPARE(unpaid.size(), 1);
    // The invoice is reduced by what was paid against it and nothing else: the
    // two payments naming it leave 40000 - 15000. The two general payments come
    // off the balance but are not tied to any invoice, so they do not settle
    // this one, which is why 25000 here is 2500 higher than the 22500 balance.
    QCOMPARE(unpaid[0].remainingCents, 25000LL);
}


void SupplierPaymentServiceTest::cash_payment_lowers_the_expected_till()
{
    // The defect this covers: paying a supplier settled their balance and wrote
    // nothing else, so money left the drawer without a trace and the session
    // reconciled as though it had never been paid. The operator saw a phantom
    // shortfall the size of the payment and no receipt to explain it.
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("cash_payment_lowers_the_expected_till.sqlite")));
    QVERIFY(f.sessionId > 0);
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد نقدي"), 80000);
    QVERIFY(supplierId > 0);

    data::CashMovementRepository movements(f.db);
    QCOMPARE(movements.sumBySessionId(f.sessionId), 0LL);

    const data::SupplierPaymentResult result =
        f.service.recordPayment(supplierId, std::nullopt, 30000, data::nowIso(), QString(),
                                core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));

    // The drawer is down by exactly what was paid: the movements sum goes to
    // -30000 and the expected total to 100000 - 30000.
    QCOMPARE(movements.sumBySessionId(f.sessionId), -30000LL);
    QCOMPARE(core::CashSessionCalculator::expectedTotalCents(100000, movements.sumBySessionId(f.sessionId)),
             70000LL);

    // One row, negative, and labelled: an operator reading the till list sees a
    // supplier payment and not a bare figure to work out themselves.
    const auto rows = movements.findBySessionId(f.sessionId);
    QCOMPARE(rows.size(), std::size_t(1));
    QCOMPARE(rows[0].type, core::cashMovementType::kSupplierPayment);
    QCOMPARE(rows[0].amountCents, -30000LL);

    // And the day closes square against it: counted what was expected, variance
    // nil. Nothing to reconcile by hand afterwards.
    QVERIFY(data::CashSessionRepository(f.db).close(f.sessionId, 70000, 70000, 0));
    const auto session = data::CashSessionRepository(f.db).findById(f.sessionId);
    QVERIFY(session.has_value());
    QCOMPARE(session->varianceCents, 0LL);

    // The supplier's balance still moved exactly as before the drawer did.
    QCOMPARE(f.service.balanceFor(supplierId), 50000LL);
}

void SupplierPaymentServiceTest::cash_payment_without_a_session_is_refused()
{
    // cash_movements.session_id is NOT NULL, so a cash payment with no session
    // cannot be written at all. Refused with a reason rather than recorded
    // silently: a payment nobody can reconcile is worse than one not taken.
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("cash_payment_without_a_session_is_refused.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد بلا وردية"), 80000);
    QVERIFY(supplierId > 0);

    const data::SupplierPaymentResult result =
        f.service.recordPayment(supplierId, std::nullopt, 30000, data::nowIso(), QString(),
                                core::SupplierPaymentMethod::Cash, std::nullopt);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());

    // Nothing landed: no payment, no movement, balance untouched.
    QCOMPARE(f.payments.findBySupplierId(supplierId).size(), std::size_t(0));
    QCOMPARE(data::CashMovementRepository(f.db).sumBySessionId(f.sessionId), 0LL);
    QCOMPARE(f.service.balanceFor(supplierId), 80000LL);
}

void SupplierPaymentServiceTest::cash_payment_refuses_a_closed_session()
{
    // The till was closed while the form was open. The session is checked at the
    // moment of writing, not when the dialog was built, so a payment cannot land
    // in a session nobody is counting any more.
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("cash_payment_refuses_a_closed_session.sqlite")));
    QVERIFY(f.sessionId > 0);
    data::CashSessionRepository sessions(f.db);
    QVERIFY(sessions.close(f.sessionId, 100000, 100000, 0));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد وردية مغلقة"), 80000);
    QVERIFY(supplierId > 0);

    const data::SupplierPaymentResult result =
        f.service.recordPayment(supplierId, std::nullopt, 30000, data::nowIso(), QString(),
                                core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    // The reason names the closed session, so an operator can tell this apart
    // from the drawer being broken.
    QVERIFY(result.error.contains(QString::number(f.sessionId)));

    QCOMPARE(f.payments.findBySupplierId(supplierId).size(), std::size_t(0));
    QCOMPARE(data::CashMovementRepository(f.db).sumBySessionId(f.sessionId), 0LL);
    QCOMPARE(f.service.balanceFor(supplierId), 80000LL);
}

void SupplierPaymentServiceTest::credit_and_bank_touch_no_cash()
{
    // Settled on the account and paid through the bank: the supplier's balance
    // moves, the drawer does not. Neither needs a session, so neither is refused
    // for want of one.
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("credit_and_bank_touch_no_cash.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد غير نقدي"), 100000);
    QVERIFY(supplierId > 0);

    const data::SupplierPaymentResult credit =
        f.service.recordPayment(supplierId, std::nullopt, 30000, data::nowIso(), QStringLiteral("آجل"),
                                core::SupplierPaymentMethod::Credit, std::nullopt);
    QVERIFY2(credit.ok, qPrintable(credit.error));
    const data::SupplierPaymentResult bank =
        f.service.recordPayment(supplierId, std::nullopt, 20000, data::nowIso(), QStringLiteral("بنك"),
                                core::SupplierPaymentMethod::Bank, std::nullopt);
    QVERIFY2(bank.ok, qPrintable(bank.error));

    QCOMPARE(f.service.balanceFor(supplierId), 50000LL);
    // The drawer saw nothing at all.
    QCOMPARE(data::CashMovementRepository(f.db).sumBySessionId(f.sessionId), 0LL);

    // And the method read back is the one that was chosen, not the column default.
    QCOMPARE(f.payments.findById(credit.paymentId)->method, core::SupplierPaymentMethod::Credit);
    QCOMPARE(f.payments.findById(bank.paymentId)->method, core::SupplierPaymentMethod::Bank);
}

void SupplierPaymentServiceTest::method_round_trips_through_the_column()
{
    // The column is what every other reader gets the method from, so what goes in
    // has to be what comes back, spelled the way the ledger stores it.
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("method_round_trips_through_the_column.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);

    const core::SupplierPaymentMethod methods[] = {core::SupplierPaymentMethod::Cash,
                                                   core::SupplierPaymentMethod::Credit,
                                                   core::SupplierPaymentMethod::Bank};
    for (core::SupplierPaymentMethod method : methods) {
        const data::SupplierPaymentResult result =
            f.service.recordPayment(supplierId, std::nullopt, 100, data::nowIso(), QString(), method,
                                    core::isCashPayment(method) ? std::optional<int>(f.sessionId)
                                                                : std::nullopt);
        QVERIFY2(result.ok, qPrintable(result.error));
        const auto stored = f.payments.findById(result.paymentId);
        QVERIFY(stored.has_value());
        QCOMPARE(stored->method, method);
        QCOMPARE(core::supplierPaymentMethodName(stored->method), core::supplierPaymentMethodName(method));
        QCOMPARE(core::parseSupplierPaymentMethod(core::supplierPaymentMethodName(method)), method);
    }

    // A spelling this build does not know reads back as "did not come out of the
    // drawer" rather than as cash, so a row written by a future build cannot
    // invent a drawer movement on the way in.
    QVERIFY(!core::parseSupplierPaymentMethod(QStringLiteral("cheque")).has_value());
    QCOMPARE(core::parseSupplierPaymentMethod(QStringLiteral("cheque"))
                 .value_or(core::SupplierPaymentMethod::Credit),
             core::SupplierPaymentMethod::Credit);
}

QTEST_GUILESS_MAIN(SupplierPaymentServiceTest)
#include "tst_payment_service.moc"
