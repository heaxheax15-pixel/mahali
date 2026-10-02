#include <QtTest/QtTest>

#include <QSqlQuery>
#include <QTemporaryDir>

#include "core/cash_movement.h"
#include "core/cash_session_calculator.h"
#include "core/supplier_payment.h"
#include "data/date_utils.h"
#include "data/database.h"
#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "data/daily_report_service.h"
#include "data/occasion_repository.h"
#include "data/occasion_service.h"
#include "data/product_repository.h"
#include "data/purchase_item_repository.h"
#include "data/purchase_repository.h"
#include "data/purchase_service.h"
#include "data/report_service.h"
#include "data/sale_service.h"
#include "data/setting_repository.h"
#include "data/stock_movement_repository.h"
#include "data/supplier_repository.h"
#include "data/supplier_payment_repository.h"
#include "data/supplier_payment_service.h"
#include "data/supplier_report_service.h"
#include "data/supplier_return_repository.h"

#include <algorithm>

using namespace app;

// One database per test, on purpose: a purchase moves stock and rewrites average
// costs, so a shared file would let the order the tests run in decide what each
// one sees.
namespace {

struct Fixture {
    explicit Fixture(const QString& path)
        : db(path)
        , purchases(db)
        , items(db)
        , products(db)
        , stockMovements(db)
        , suppliers(db)
        , supplierPayments(db)
        , service(db, purchases, items, products, stockMovements, suppliers, supplierPayments)
    {
        // Every fixture opens a till. Only the tests that actually hand money over
        // with the invoice name it: an invoice paid at the counter has to be
        // booked against a session so the drawer can be said to have given the
        // money up, and an invoice paid on the account names none, because no
        // drawer is involved. Which tests pass sessionId is the whole difference
        // between the two paths.
        sessionId = data::CashSessionRepository(db).open(100000);
    }

    data::Database db;
    data::PurchaseRepository purchases;
    data::PurchaseItemRepository items;
    data::ProductRepository products;
    data::StockMovementRepository stockMovements;
    data::SupplierRepository suppliers;
    data::SupplierPaymentRepository supplierPayments;
    data::PurchaseService service;
    int sessionId = 0;
};

int addSupplier(data::SupplierRepository& suppliers, const QString& name)
{
    core::Supplier supplier;
    supplier.name = name;
    return suppliers.save(supplier);
}

int addProduct(data::ProductRepository& products, const QString& name, long long costPriceCents)
{
    core::Product product;
    product.name = name;
    product.costPriceCents = costPriceCents;
    product.salePriceCents = costPriceCents;
    return products.save(product);
}

core::PurchaseItem makeLine(std::optional<int> productId, long long quantity, long long unitPriceCents)
{
    core::PurchaseItem line;
    line.productId = productId;
    line.description = QStringLiteral("Purchased line");
    line.quantity = quantity;
    line.unitPriceCents = unitPriceCents;
    line.totalCents = quantity * unitPriceCents;
    return line;
}

core::Purchase makePurchase(int supplierId, long long totalCents, long long paidCents, bool addToStock,
                            long long vatCents = 0)
{
    core::Purchase purchase;
    purchase.supplierId = supplierId;
    purchase.totalCents = totalCents;
    // The header is filled in the way the dialog fills it: the total is what is
    // owed, and the sub-total is what is left once the VAT is taken out.
    purchase.subtotalCents = totalCents - vatCents;
    purchase.vatCents = vatCents;
    purchase.paidCents = paidCents;
    purchase.addToStock = addToStock;
    return purchase;
}

int countRows(const data::Database& db, const QString& table)
{
    QSqlQuery query(db.handle());
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

} // namespace

class PurchaseServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void cash_purchase_simple();
    void credit_purchase();
    void partial_payment();
    void purchase_with_vat();
    void payment_above_total_fails();
    void no_stock_purchase();
    void pmp_calculation();
    void empty_items_fails();
    void rollback_on_bad_product();
    void purchase_with_null_strings();
    void purchase_carries_active_occasion();
    void purchase_no_occasion_when_inactive();

    void cash_invoice_lowers_the_expected_till();
    void unpaid_invoice_needs_no_session();
    void cash_invoice_without_a_session_is_refused();
    void credit_invoice_moves_no_cash();

    void void_cash_purchase_restores_drawer_and_balance();
    void void_purchase_allowed_without_later_movements();
    void void_purchase_rejected_when_later_stock_movements();
    void void_purchase_rejected_when_later_payments();
    void void_purchase_rejected_when_later_credit_payment();
    void void_cash_purchase_with_initial_payment_succeeds();
    void void_purchase_never_leaves_negative_stock();
    void void_purchase_supplier_balance_restored();
    void void_purchase_reversal_appears_in_supplier_report();

private:
    QTemporaryDir m_dir;
};

void PurchaseServiceTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

void PurchaseServiceTest::cash_purchase_simple()
{
    Fixture f(m_dir.filePath(QStringLiteral("cash_purchase_simple.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد نقدي"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("شاي"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 500000, true);
    const QVector<core::PurchaseItem> lines = {makeLine(productId, 100, 5000)};

    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, lines, core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.purchaseId > 0);

    const auto movements = f.stockMovements.findByProductId(productId);
    QCOMPARE(movements.size(), std::size_t(1));
    QCOMPARE(movements[0].delta, 100LL);
    QCOMPARE(movements[0].reason, QStringLiteral("purchase"));
    QCOMPARE(movements[0].reference, QStringLiteral("Purchase #%1").arg(result.purchaseId));

    const auto product = f.products.findById(productId);
    QVERIFY(product.has_value());
    QCOMPARE(product->quantity, 100LL);
    QCOMPARE(product->costPriceCents, 5000LL);

    // The invoice total is what the supplier is owed and it lives in purchases;
    // the only thing written here is the payment that offsets it, and it is
    // tied to this invoice so the two can be read together.
    const auto payments = f.supplierPayments.findByPurchaseId(result.purchaseId);
    QCOMPARE(payments.size(), std::size_t(1));
    QCOMPARE(payments[0].amountCents, 500000LL);
    QCOMPARE(payments[0].supplierId, supplierId);
    QVERIFY(payments[0].purchaseId.has_value());
    QCOMPARE(*payments[0].purchaseId, result.purchaseId);
    QCOMPARE(f.supplierPayments.findBySupplierId(supplierId).size(), std::size_t(1));
}

void PurchaseServiceTest::credit_purchase()
{
    Fixture f(m_dir.filePath(QStringLiteral("credit_purchase.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد آجل"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("سكر"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 0, true);
    const QVector<core::PurchaseItem> lines = {makeLine(productId, 100, 5000)};

    const data::PurchaseResult result = f.service.recordPurchase(purchase, lines);
    QVERIFY2(result.ok, qPrintable(result.error));

    const auto movements = f.stockMovements.findByProductId(productId);
    QCOMPARE(movements.size(), std::size_t(1));
    QCOMPARE(movements[0].delta, 100LL);

    // Nothing was paid, so there is no payment against the invoice at all: the
    // whole amount stays owed and is read from purchases, not from here.
    QCOMPARE(f.supplierPayments.findBySupplierId(supplierId).size(), std::size_t(0));
}

void PurchaseServiceTest::partial_payment()
{
    Fixture f(m_dir.filePath(QStringLiteral("partial_payment.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد جزئي"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("زيت"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 1000000, 400000, true);
    const QVector<core::PurchaseItem> lines = {makeLine(productId, 100, 10000)};

    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, lines, core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));

    const auto movements = f.stockMovements.findByProductId(productId);
    QCOMPARE(movements.size(), std::size_t(1));
    QCOMPARE(movements[0].delta, 100LL);

    // One payment, for the part that was settled, against this one invoice.
    const auto payments = f.supplierPayments.findByPurchaseId(result.purchaseId);
    QCOMPARE(payments.size(), std::size_t(1));
    QCOMPARE(payments[0].amountCents, 400000LL);
    QVERIFY(payments[0].purchaseId.has_value());
    QCOMPARE(*payments[0].purchaseId, result.purchaseId);
    QCOMPARE(f.supplierPayments.findBySupplierId(supplierId).size(), std::size_t(1));
}

void PurchaseServiceTest::purchase_with_vat()
{
    Fixture f(m_dir.filePath(QStringLiteral("purchase_with_vat.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد مائل"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("ماء"), 0);
    QVERIFY(productId > 0);

    // 100 units at 10.00 is 1000.00, and the invoice carries 19% of that on top.
    const core::Purchase purchase = makePurchase(supplierId, 119000, 119000, true, 19000);
    const QVector<core::PurchaseItem> lines = {makeLine(productId, 100, 1000)};

    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, lines, core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));

    // The VAT is stored beside the sub-total, not folded into it and not dropped
    // on the way in: the three columns have to say what the invoice says.
    const auto stored = f.purchases.findById(result.purchaseId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->subtotalCents, 100000LL);
    QCOMPARE(stored->vatCents, 19000LL);
    QCOMPARE(stored->totalCents, 119000LL);

    // Paid in full against a total that now carries the VAT, so nothing is left
    // owed. While the total was the sub-total alone this same payment left a
    // residue exactly the size of the VAT, and the balance called it a debt.
    const auto payments = f.supplierPayments.findByPurchaseId(result.purchaseId);
    QCOMPARE(payments.size(), std::size_t(1));
    QCOMPARE(payments[0].amountCents, 119000LL);
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 0LL);

    // The sub-total is what the stock was bought for, so the average cost must
    // not pick up the VAT the operator paid on the invoice.
    const auto product = f.products.findById(productId);
    QVERIFY(product.has_value());
    QCOMPARE(product->costPriceCents, 1000LL);
}

void PurchaseServiceTest::payment_above_total_fails()
{
    Fixture f(m_dir.filePath(QStringLiteral("payment_above_total_fails.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد دفعة زائدة"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("عسل"), 0);
    QVERIFY(productId > 0);

    // 100.00 of goods, 19.00 of VAT, and 120.00 handed over. The 1.00 over is
    // not a debt: it is money the supplier does not owe, and accepting it here
    // would leave the balance carrying a credit nobody asked for.
    const core::Purchase purchase = makePurchase(supplierId, 119000, 120000, true, 19000);
    const QVector<core::PurchaseItem> lines = {makeLine(productId, 100, 1000)};

    const data::PurchaseResult result = f.service.recordPurchase(purchase, lines);

    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("paid amount 120000 exceeds the invoice total 119000"));
    // Refused before anything was written, so neither the invoice nor the
    // payment it would have carried is left behind.
    QCOMPARE(countRows(f.db, QStringLiteral("purchases")), 0);
    QCOMPARE(countRows(f.db, QStringLiteral("supplier_payments")), 0);
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 0LL);
}

void PurchaseServiceTest::no_stock_purchase()
{
    Fixture f(m_dir.filePath(QStringLiteral("no_stock_purchase.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد خدمات"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("خدمة"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 0, false);
    const QVector<core::PurchaseItem> lines = {makeLine(productId, 100, 5000)};

    const data::PurchaseResult result = f.service.recordPurchase(purchase, lines);
    QVERIFY2(result.ok, qPrintable(result.error));

    // addToStock = 0: an invoice that only adds to the bill, not the shelves.
    QCOMPARE(f.stockMovements.findByProductId(productId).size(), std::size_t(0));
    const auto product = f.products.findById(productId);
    QVERIFY(product.has_value());
    QCOMPARE(product->quantity, 0LL);
    QCOMPARE(product->costPriceCents, 0LL);

    // Nothing was paid, so no payment is recorded either way. The invoice is
    // still owed in full: addToStock decides what lands on the shelf, not what
    // the supplier is owed.
    QCOMPARE(f.supplierPayments.findBySupplierId(supplierId).size(), std::size_t(0));
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 500000LL);
}

void PurchaseServiceTest::pmp_calculation()
{
    Fixture f(m_dir.filePath(QStringLiteral("pmp_calculation.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد PMP"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("دقيق"), 5000);
    QVERIFY(productId > 0);
    // save() always starts a product at zero, so the opening stock is a movement
    // like any other: 100 units already on the shelf.
    f.products.adjustStock(productId, 100, QStringLiteral("opening stock"));
    const auto opening = f.products.findById(productId);
    QVERIFY(opening.has_value());
    QCOMPARE(opening->quantity, 100LL);

    // 100 more at 7000 on top of 100 at 5000: (100*5000 + 100*7000) / 200.
    const core::Purchase purchase = makePurchase(supplierId, 700000, 0, true);
    const QVector<core::PurchaseItem> lines = {makeLine(productId, 100, 7000)};

    const data::PurchaseResult result = f.service.recordPurchase(purchase, lines);
    QVERIFY2(result.ok, qPrintable(result.error));

    const auto product = f.products.findById(productId);
    QVERIFY(product.has_value());
    QCOMPARE(product->quantity, 200LL);
    QCOMPARE(product->costPriceCents, 6000LL);
}

void PurchaseServiceTest::empty_items_fails()
{
    Fixture f(m_dir.filePath(QStringLiteral("empty_items_fails.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد فارغ"));
    QVERIFY(supplierId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 0, 0, true);
    const data::PurchaseResult result = f.service.recordPurchase(purchase, QVector<core::PurchaseItem>());

    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("purchase items are empty"));
    QCOMPARE(countRows(f.db, QStringLiteral("purchases")), 0);
}

void PurchaseServiceTest::rollback_on_bad_product()
{
    Fixture f(m_dir.filePath(QStringLiteral("rollback_on_bad_product.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد خطأ"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("منتج سليم"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 0, true);
    const QVector<core::PurchaseItem> lines = {
        makeLine(productId, 10, 5000),
        makeLine(999999, 10, 45000),
    };

    const data::PurchaseResult result = f.service.recordPurchase(purchase, lines);

    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    // The good line must not survive the bad one: no half-purchase.
    QCOMPARE(countRows(f.db, QStringLiteral("purchases")), 0);
    QCOMPARE(countRows(f.db, QStringLiteral("purchase_items")), 0);
    QCOMPARE(countRows(f.db, QStringLiteral("stock_movements")), 0);
}

void PurchaseServiceTest::purchase_with_null_strings()
{
    Fixture f(m_dir.filePath(QStringLiteral("purchase_with_null_strings.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد بلا أرقام"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("منتج بلا وصف"), 0);
    QVERIFY(productId > 0);

    // A null QString is what a caller that never touched these fields holds, and
    // the driver binds it as SQL NULL. Both columns are NOT NULL, so the header
    // has to reach the database as empty strings or the whole purchase is lost.
    core::Purchase purchase = makePurchase(supplierId, 500000, 0, true);
    purchase.invoiceNumber = QString();
    purchase.note = QString();
    QVERIFY(purchase.invoiceNumber.isNull());
    QVERIFY(purchase.note.isNull());

    core::PurchaseItem line = makeLine(productId, 100, 5000);
    line.description = QString();
    line.unit = QString();

    const data::PurchaseResult result = f.service.recordPurchase(purchase, {line});
    QVERIFY2(result.ok, qPrintable(result.error));

    const auto stored = f.purchases.findById(result.purchaseId);
    QVERIFY(stored.has_value());
    QVERIFY(stored->invoiceNumber.isEmpty());
    QVERIFY(stored->note.isEmpty());
    QCOMPARE(stored->totalCents, 500000LL);

    const auto lines = f.items.findByPurchase(result.purchaseId);
    QCOMPARE(lines.size(), 1);
    QVERIFY(lines[0].description.isEmpty());
    QVERIFY(lines[0].unit.isEmpty());

    // The same header straight through the repository, so the guard is proved
    // where it lives rather than only through the service that also normalises
    // the fields before handing them over.
    core::Purchase direct = purchase;
    direct.purchasedAt = QString();
    direct.createdAt = QString();
    const int directId = f.purchases.insert(direct);
    QVERIFY2(directId > 0, qPrintable(f.db.lastError()));
    const auto directStored = f.purchases.findById(directId);
    QVERIFY(directStored.has_value());
    QVERIFY(directStored->invoiceNumber.isEmpty());
    QVERIFY(directStored->note.isEmpty());
    QVERIFY(directStored->purchasedAt.isEmpty());
    QVERIFY(directStored->createdAt.isEmpty());
}

void PurchaseServiceTest::purchase_carries_active_occasion()
{
    Fixture f(m_dir.filePath(QStringLiteral("purchase_carries_active_occasion.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد موسم"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("تمر"), 0);
    QVERIFY(productId > 0);

    data::OccasionRepository occasions(f.db);
    data::SettingRepository settings(f.db);
    data::OccasionService occasionService(f.db, occasions, settings);

    core::Occasion occasion;
    occasion.name = QStringLiteral("موسم التمور");
    occasion.startsAt = data::nowIso();
    occasion.endsAt = data::nowIso();
    occasion.createdAt = data::nowIso();
    const int occasionId = occasions.insert(occasion);
    QVERIFY2(occasionId > 0, qPrintable(f.db.lastError()));
    QVERIFY(occasionService.activate(occasionId));

    const data::PurchaseResult result = f.service.recordPurchase(makePurchase(supplierId, 500000, 0, true),
                                                                  {makeLine(productId, 100, 5000)});
    QVERIFY2(result.ok, qPrintable(result.error));

    const auto stored = f.purchases.findById(result.purchaseId);
    QVERIFY(stored.has_value());
    QVERIFY(stored->occasionId.has_value());
    QCOMPARE(*stored->occasionId, occasionId);

    // Read back as a number, not the text of one, so a report grouping by
    // occasion joins on an integer rather than on a string.
    QSqlQuery query(f.db.handle());
    QVERIFY(query.exec(QStringLiteral("SELECT occasion_id FROM purchases WHERE id = %1").arg(result.purchaseId)));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), occasionId);
}

void PurchaseServiceTest::purchase_no_occasion_when_inactive()
{
    Fixture f(m_dir.filePath(QStringLiteral("purchase_no_occasion_when_inactive.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد عادي"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("سكر"), 0);
    QVERIFY(productId > 0);

    // An occasion exists but is switched off. current() reads through the active
    // flag, so a purchase must not be stamped with it.
    data::OccasionRepository occasions(f.db);
    data::SettingRepository settings(f.db);
    data::OccasionService occasionService(f.db, occasions, settings);

    core::Occasion occasion;
    occasion.name = QStringLiteral("تخفيضات متوقفة");
    occasion.startsAt = data::nowIso();
    occasion.endsAt = data::nowIso();
    occasion.createdAt = data::nowIso();
    const int occasionId = occasions.insert(occasion);
    QVERIFY(occasionId > 0);
    QVERIFY(occasions.setActive(occasionId, false));
    // The setting still points at it, which is exactly the case current() has to
    // refuse rather than pass on.
    settings.set(QStringLiteral("active_occasion_id"), QString::number(occasionId));
    QVERIFY(!occasionService.current().has_value());

    const data::PurchaseResult result = f.service.recordPurchase(makePurchase(supplierId, 500000, 0, true),
                                                                  {makeLine(productId, 100, 5000)});
    QVERIFY2(result.ok, qPrintable(result.error));

    const auto stored = f.purchases.findById(result.purchaseId);
    QVERIFY(stored.has_value());
    QVERIFY(!stored->occasionId.has_value());

    // The column holds a real SQL NULL rather than 0, so a report can tell a
    // purchase made outside any occasion from one made during occasion zero.
    QSqlQuery query(f.db.handle());
    QVERIFY(query.exec(QStringLiteral("SELECT occasion_id FROM purchases WHERE id = %1").arg(result.purchaseId)));
    QVERIFY(query.next());
    QVERIFY2(query.value(0).isNull(), "occasion_id must be NULL, not 0");
}

void PurchaseServiceTest::cash_invoice_lowers_the_expected_till()
{
    // The defect this covers: money handed to a supplier with the invoice left no
    // trace in the drawer. The payment settled the supplier and nothing else, so
    // the session still expected the full opening float plus the day's sales —
    // the drawer was short by exactly what had been paid out and the operator
    // closed the day staring at a deficit that no receipt explained.
    Fixture f(m_dir.filePath(QStringLiteral("cash_invoice_lowers_the_expected_till.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد نقدي"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("شاي"), 0);
    QVERIFY(productId > 0);

    // The drawer starts on the opening float alone. sumBySessionId counts the
    // movements and nothing else, so it is the float that has to be added on top
    // to get what the drawer is expected to hold.
    data::CashMovementRepository movements(f.db);
    QVERIFY(f.sessionId > 0);
    QCOMPARE(movements.sumBySessionId(f.sessionId), 0LL);
    QCOMPARE(core::CashSessionCalculator::expectedTotalCents(100000, 0), 100000LL);

    // A day's takings first, so the drawer holds something to be paid out of.
    // 100 units bought at 50.00, sold again at 70.00: the till takes 7000.00.
    f.products.adjustStock(productId, 100, QStringLiteral("purchase"));
    core::Product priced = *f.products.findById(productId);
    priced.salePriceCents = 7000;
    f.products.save(priced);
    core::SaleItem sold;
    sold.productId = productId;
    sold.quantity = 100;
    QVERIFY(data::SaleService(f.db).recordSale({sold}, f.sessionId, QStringLiteral("dev-test"), false).ok);
    QCOMPARE(movements.sumBySessionId(f.sessionId), 700000LL);

    // The invoice is settled in part, out of the drawer: 4000.00 of a 5000.00
    // invoice. paidCents above zero with method Cash, so a session is named.
    const core::Purchase purchase = makePurchase(supplierId, 500000, 400000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));

    // 700000 of takings less the 400000 handed to the supplier: the movements sum
    // drops by exactly the amount that left the drawer.
    QCOMPARE(movements.sumBySessionId(f.sessionId), 300000LL);
    const long long expected = core::CashSessionCalculator::expectedTotalCents(100000, 300000);
    QCOMPARE(expected, 400000LL);

    // One movement for the payment, negative, and it says what it was for rather
    // than showing up as a bare figure the operator has to recognise.
    const auto rows = movements.findBySessionId(f.sessionId);
    QCOMPARE(rows.size(), std::size_t(2));
    QCOMPARE(rows[1].type, core::cashMovementType::kSupplierPayment);
    QCOMPARE(rows[1].amountCents, -400000LL);

    // And the day closes square. Counted 400000 against 400000 expected, variance
    // nil: the payment is accounted for, not a phantom shortfall on top of it.
    QVERIFY(data::CashSessionRepository(f.db).close(f.sessionId, expected, expected, 0));
    const auto session = data::CashSessionRepository(f.db).findById(f.sessionId);
    QVERIFY(session.has_value());
    QCOMPARE(session->status, QStringLiteral("closed"));
    QCOMPARE(session->expectedCents, 400000LL);
    QCOMPARE(session->varianceCents, 0LL);
}

void PurchaseServiceTest::unpaid_invoice_needs_no_session()
{
    // Nothing changed hands, so nothing has to be booked: the invoice is recorded
    // with no session at all. Refusing this would mean a shop with its drawer
    // closed could not take delivery of goods.
    Fixture f(m_dir.filePath(QStringLiteral("unpaid_invoice_needs_no_session.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد آجل"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("زيت"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 0, true);
    const data::PurchaseResult result = f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)});
    QVERIFY2(result.ok, qPrintable(result.error));

    // No payment was recorded, so nothing may have reached the drawer and the
    // session still holds exactly its opening float.
    QCOMPARE(f.supplierPayments.findBySupplierId(supplierId).size(), std::size_t(0));
    QCOMPARE(data::CashMovementRepository(f.db).sumBySessionId(f.sessionId), 0LL);
}

void PurchaseServiceTest::cash_invoice_without_a_session_is_refused()
{
    // Paid out of the drawer, but no session to book it against. Refused outright
    // rather than recorded: the alternative is a payment on the ledger that the
    // till never saw, which is the same defect this whole path exists to close.
    Fixture f(m_dir.filePath(QStringLiteral("cash_invoice_without_a_session_is_refused.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد بلا وردية"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("عسل"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 500000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, std::nullopt);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());

    // Nothing at all was written: no invoice, no stock, no payment, no movement.
    QCOMPARE(countRows(f.db, QStringLiteral("purchases")), 0);
    QCOMPARE(countRows(f.db, QStringLiteral("supplier_payments")), 0);
    QCOMPARE(countRows(f.db, QStringLiteral("cash_movements")), 0);
    const auto product = f.products.findById(productId);
    QVERIFY(product.has_value());
    QCOMPARE(product->quantity, 0LL);
}

void PurchaseServiceTest::credit_invoice_moves_no_cash()
{
    // Settled on the account: the supplier's balance drops and the drawer is
    // untouched. This is the case that used to be impossible to record without
    // pretending the money had left the till.
    Fixture f(m_dir.filePath(QStringLiteral("credit_invoice_moves_no_cash.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد ائتمان"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("سكر"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 500000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Credit, std::nullopt);
    QVERIFY2(result.ok, qPrintable(result.error));

    const auto payments = f.supplierPayments.findByPurchaseId(result.purchaseId);
    QCOMPARE(payments.size(), std::size_t(1));
    QCOMPARE(payments[0].amountCents, 500000LL);
    QCOMPARE(payments[0].method, core::SupplierPaymentMethod::Credit);
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 0LL);
    // The drawer never saw it, and the session is exactly as it was opened.
    QCOMPARE(data::CashMovementRepository(f.db).sumBySessionId(f.sessionId), 0LL);
}

// A void of a cash purchase returns the money to the drawer and reverses the
// supplier payment. The supplier balance and drawer are both restored.
void PurchaseServiceTest::void_cash_purchase_restores_drawer_and_balance()
{
    Fixture f(m_dir.filePath(QStringLiteral("void_cash_purchase.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد نقدي"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("زيت"), 0);
    QVERIFY(productId > 0);
    f.products.adjustStock(productId, 10, QStringLiteral("opening"));

    const core::Purchase purchase = makePurchase(supplierId, 500000, 500000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    const int purchaseId = result.purchaseId;
    const auto initialPayments = f.supplierPayments.findByPurchaseId(purchaseId);
    QCOMPARE(initialPayments.size(), std::size_t(1));
    QVERIFY(initialPayments.front().isPurchaseInitialPayment);

    // An invoice paid in full leaves nothing owed, and the drawer is 500000 short
    // of where it started.
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 0LL);
    QCOMPARE(data::CashMovementRepository(f.db).sumBySessionId(f.sessionId), -500000LL);

    // Void the purchase.
    const data::PurchaseResult voidResult = f.service.voidPurchase(purchaseId, f.sessionId);
    QVERIFY2(voidResult.ok, qPrintable(voidResult.error));

    const auto voidPurchase = f.purchases.findById(voidResult.purchaseId);
    QVERIFY(voidPurchase.has_value());
    QCOMPARE(voidPurchase->totalCents, -500000LL);
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 0LL);

    // Supplier balance back to 0, drawer gets +500000 back.
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 0LL);
    QCOMPARE(data::CashMovementRepository(f.db).sumBySessionId(f.sessionId), 0LL);

    // Stock returned (was 0 after sale of 100, now back to 10).
    const auto product = f.products.findById(productId);
    QVERIFY(product.has_value());
    QCOMPARE(product->quantity, 10LL);

    // Supplier payment reversal row exists as a negative amount pointing back at
    // the payment the purchase booked, so the two net to zero.
    const auto spRows = f.supplierPayments.findByPurchaseId(purchaseId);
    QVERIFY(spRows.size() >= 2); // original + reversal
    const auto originalPayment = std::find_if(spRows.cbegin(), spRows.cend(),
                                              [](const core::SupplierPayment& row) { return row.amountCents > 0; });
    QVERIFY(originalPayment != spRows.cend());
    const auto reversal = std::find_if(spRows.cbegin(), spRows.cend(),
                                       [](const core::SupplierPayment& row) { return row.amountCents < 0; });
    QVERIFY(reversal != spRows.cend());
    QCOMPARE(reversal->amountCents, -originalPayment->amountCents);
    QCOMPARE(reversal->reversedId, originalPayment->id);
}

// Nothing has touched the products since the purchase, so the void is allowed.
void PurchaseServiceTest::void_purchase_allowed_without_later_movements()
{
    Fixture f(m_dir.filePath(QStringLiteral("void_allowed.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("شاي"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 300000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));

    const data::PurchaseResult voidBefore = f.service.voidPurchase(result.purchaseId, f.sessionId);
    QVERIFY2(voidBefore.ok, qPrintable(voidBefore.error));
}

// Void purchase is refused if any product has moved stock since it was bought.
void PurchaseServiceTest::void_purchase_rejected_when_later_stock_movements()
{
    Fixture f(m_dir.filePath(QStringLiteral("void_later_stock.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("شاي"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 500000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    const int purchaseId = result.purchaseId;
    QCOMPARE(f.products.findById(productId)->quantity, 100LL);

    // The goods are sold after the purchase, so the shelf no longer holds
    // exactly what the invoice put there.
    core::SaleItem sold;
    sold.productId = productId;
    sold.quantity = 5;
    sold.unitPriceCents = 7000;
    const auto sale =
        data::SaleService(f.db).recordSale({sold}, f.sessionId, QStringLiteral("dev"), false);
    QVERIFY2(sale.ok, qPrintable(sale.error));
    QCOMPARE(f.products.findById(productId)->quantity, 95LL);

    // Voiding now would hand back 100 against a shelf holding 95.
    const data::PurchaseResult voidResult = f.service.voidPurchase(purchaseId, f.sessionId);
    QVERIFY(!voidResult.ok);
    QVERIFY2(!voidResult.error.isEmpty(), qPrintable(voidResult.error));
    QVERIFY(voidResult.error.contains(QStringLiteral("moved stock")));

    // Refused means refused: the level, the invoice and the money all stand. The
    // drawer still holds what the purchase took out plus what the sale brought
    // in (5 at 7000), with none of it given back.
    QCOMPARE(f.products.findById(productId)->quantity, 95LL);
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 0LL);
    QCOMPARE(data::CashMovementRepository(f.db).sumBySessionId(f.sessionId), -500000LL + 35000LL);
}

// Void purchase is refused if there are later payments on the same invoice.
void PurchaseServiceTest::void_purchase_rejected_when_later_payments()
{
    Fixture f(m_dir.filePath(QStringLiteral("void_later_payment.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("سكر"), 0);
    QVERIFY(productId > 0);

    // The invoice is for 500000 and 300000 is paid at the time, so 200000 stays
    // open for the payment made afterwards.
    const core::Purchase purchase = makePurchase(supplierId, 500000, 300000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    const int purchaseId = result.purchaseId;
    QCOMPARE(f.purchases.findById(purchaseId)->totalCents, 500000LL);

    // Make a LATER payment on the same invoice (not the initial one).
    data::SupplierPaymentRepository spRepo(f.db);
    data::SupplierPaymentService spSvc(f.db, spRepo, f.suppliers, f.purchases);
    const auto laterResult = spSvc.recordPayment(supplierId, purchaseId, 200000, data::nowIso(), QString(),
                                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(laterResult.ok, qPrintable(laterResult.error));

    // Now try to void — should be refused because of the later payment.
    const data::PurchaseResult voidResult = f.service.voidPurchase(purchaseId, f.sessionId);
    QVERIFY(!voidResult.ok);
    QVERIFY2(!voidResult.error.isEmpty(), qPrintable(voidResult.error));
    QVERIFY(voidResult.error.contains(QStringLiteral("payment")));
}

// Void purchase with later supplier payment (credit) also refused.
void PurchaseServiceTest::void_purchase_rejected_when_later_credit_payment()
{
    Fixture f(m_dir.filePath(QStringLiteral("void_later_credit_payment.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("زيت"), 0);
    QVERIFY(productId > 0);

    // The invoice is for 500000 and 300000 is paid at the time, so 200000 stays
    // open for the payment made afterwards.
    const core::Purchase purchase = makePurchase(supplierId, 500000, 300000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    const int purchaseId = result.purchaseId;
    QCOMPARE(f.purchases.findById(purchaseId)->totalCents, 500000LL);

    // Later credit payment on the same invoice. No session: credit never
    // touches the drawer.
    data::SupplierPaymentRepository spRepo(f.db);
    data::SupplierPaymentService spSvc(f.db, spRepo, f.suppliers, f.purchases);
    const auto laterResult = spSvc.recordPayment(supplierId, purchaseId, 200000, data::nowIso(), QString(),
                                                 core::SupplierPaymentMethod::Credit, std::nullopt);
    QVERIFY2(laterResult.ok, qPrintable(laterResult.error));

    // Try to void — should be refused.
    const data::PurchaseResult voidResult = f.service.voidPurchase(purchaseId, f.sessionId);
    QVERIFY(!voidResult.ok);
    QVERIFY2(!voidResult.error.isEmpty(), qPrintable(voidResult.error));
    QVERIFY(voidResult.error.contains(QStringLiteral("payment")));
}

// Void purchase of a cash invoice at purchase time succeeds and restores drawer.
void PurchaseServiceTest::void_cash_purchase_with_initial_payment_succeeds()
{
    Fixture f(m_dir.filePath(QStringLiteral("void_initial_payment.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("عسل"), 0);
    QVERIFY(productId > 0);

    // Purchase paid in full at creation.
    const core::Purchase purchase = makePurchase(supplierId, 500000, 500000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    const int purchaseId = result.purchaseId;

    // Void it — should succeed because the payment was the INITIAL one.
    const data::PurchaseResult voidResult = f.service.voidPurchase(purchaseId, f.sessionId);
    QVERIFY2(voidResult.ok, qPrintable(voidResult.error));

    // Drawer back to zero, supplier balance zero.
    QCOMPARE(data::CashMovementRepository(f.db).sumBySessionId(f.sessionId), 0LL);
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 0LL);
}

// Voiding never drives stock below zero. Handing stock back can only add, and
    // once the goods have moved on the void is refused instead, so the level
    // left on the shelf is whatever the shop really holds.
void PurchaseServiceTest::void_purchase_never_leaves_negative_stock()
{
    Fixture f(m_dir.filePath(QStringLiteral("void_negative_stock.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("شاي"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 500000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(f.products.findById(productId)->quantity, 100LL);

    const data::PurchaseResult voidResult = f.service.voidPurchase(result.purchaseId, f.sessionId);
    QVERIFY2(voidResult.ok, qPrintable(voidResult.error));

    // 100 went on and came straight back off: the shelf is empty, not below it.
    QCOMPARE(f.products.findById(productId)->quantity, 0LL);
}

// Supplier balance is correctly restored after void.
void PurchaseServiceTest::void_purchase_supplier_balance_restored()
{
    Fixture f(m_dir.filePath(QStringLiteral("void_balance.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("زيت"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 300000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    const int purchaseId = result.purchaseId;

    // 500000 invoiced, 300000 paid: 200000 is still owed.
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 200000LL);

    const data::PurchaseResult voidResult = f.service.voidPurchase(purchaseId, f.sessionId);
    QVERIFY2(voidResult.ok, qPrintable(voidResult.error));

    // Nothing owed: the void invoice cancels the 500000 and the reversal cancels
    // the 300000 that had been paid.
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 0LL);
}

// Void purchase of a cash invoice paid in full succeeds: the drawer's money
    // comes back and the supplier is owed nothing.
void PurchaseServiceTest::void_purchase_reversal_appears_in_supplier_report()
{
    Fixture f(m_dir.filePath(QStringLiteral("void_daily_report.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, QStringLiteral("شاي"), 0);
    QVERIFY(productId > 0);

    const core::Purchase purchase = makePurchase(supplierId, 500000, 500000, true);
    const data::PurchaseResult result =
        f.service.recordPurchase(purchase, {makeLine(productId, 100, 5000)},
                                 core::SupplierPaymentMethod::Cash, f.sessionId);
    QVERIFY2(result.ok, qPrintable(result.error));
    const int purchaseId = result.purchaseId;

    const data::PurchaseResult voidResult = f.service.voidPurchase(purchaseId, f.sessionId);
    QVERIFY2(voidResult.ok, qPrintable(voidResult.error));

    // The money back is a cash movement of its own type, pointing at the
    // purchase it undoes, and it goes in as a positive so the session nets zero.
    data::CashMovementRepository movements(f.db);
    const auto rows = movements.findBySessionId(f.sessionId);
    bool foundReversal = false;
    for (const auto& m : rows) {
        if (m.type == core::cashMovementType::kSupplierPaymentReversal) {
            foundReversal = true;
            QCOMPARE(m.amountCents, 500000LL);
            QCOMPARE(m.refType, QStringLiteral("purchase"));
            QCOMPARE(m.refId, purchaseId);
        }
    }
    QVERIFY2(foundReversal, "cash movements should contain supplier_payment_reversal");
    QCOMPARE(movements.sumBySessionId(f.sessionId), 0LL);

    data::SaleRepository sales(f.db);
    data::ExpenseRepository expenses(f.db);
    data::OwnerDrawingRepository drawings(f.db);
    data::CustomerTransactionRepository customerTransactions(f.db);
    data::CashSessionRepository cashSessions(f.db);
    const data::DailyReport daily = data::DailyReportService(
        f.db, sales, expenses, drawings, customerTransactions, cashSessions)
        .forDay(QDate::currentDate().toString(Qt::ISODate));
    bool dailyHasReversal = false;
    for (const auto& line : daily.cashLines) {
        if (line.type == core::cashMovementType::kSupplierPaymentReversal) {
            dailyHasReversal = true;
            QCOMPARE(line.count, 1);
            QCOMPARE(line.sumCents, 500000LL);
        }
    }
    QVERIFY(dailyHasReversal);

    const data::StoreReport range = data::ReportService(f.db).build(
        QDateTime::currentDateTime().addDays(-1), QDateTime::currentDateTime().addDays(1));
    bool rangeHasReversal = false;
    for (const auto& line : range.cashLines) {
        if (line.type == core::cashMovementType::kSupplierPaymentReversal) {
            rangeHasReversal = true;
            QCOMPARE(line.count, 1);
            QCOMPARE(line.sumCents, 500000LL);
        }
    }
    QVERIFY(rangeHasReversal);

    // And the supplier's own record shows both halves of the reversal, so the
    // period reads as nothing owed rather than as an unexplained gap.
    data::SupplierReturnRepository returns(f.db);
    data::SupplierReportService reports(f.db, f.suppliers, f.purchases, f.supplierPayments, returns);
    const data::SupplierPeriodReport period =
        reports.periodReport(supplierId, QStringLiteral("2000-01-01T00:00:00"),
                             QStringLiteral("2999-12-31T23:59:59"));
    QCOMPARE(period.purchasesCents, 0LL);   // invoice and its void cancel
    QCOMPARE(period.paymentsCents, 0LL);    // payment and its reversal cancel
    QCOMPARE(period.netChangeCents, 0LL);
    QCOMPARE(period.payments.size(), 2);
    bool reportHasReversal = false;
    for (const core::SupplierPayment& row : period.payments) {
        if (row.amountCents < 0) {
            reportHasReversal = true;
            QCOMPARE(row.amountCents, -500000LL);
        }
    }
    QVERIFY2(reportHasReversal, "the supplier report should list the reversed payment");
}

QTEST_GUILESS_MAIN(PurchaseServiceTest)
#include "tst_purchase_service.moc"
