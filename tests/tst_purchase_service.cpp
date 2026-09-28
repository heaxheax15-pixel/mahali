#include <QtTest/QtTest>

#include <QSqlQuery>
#include <QTemporaryDir>

#include "data/database.h"
#include "data/product_repository.h"
#include "data/purchase_item_repository.h"
#include "data/purchase_repository.h"
#include "data/purchase_service.h"
#include "data/stock_movement_repository.h"
#include "data/supplier_repository.h"
#include "data/supplier_transaction_repository.h"

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
        , supplierTxs(db)
        , service(db, purchases, items, products, stockMovements, suppliers, supplierTxs)
    {
    }

    data::Database db;
    data::PurchaseRepository purchases;
    data::PurchaseItemRepository items;
    data::ProductRepository products;
    data::StockMovementRepository stockMovements;
    data::SupplierRepository suppliers;
    data::SupplierTransactionRepository supplierTxs;
    data::PurchaseService service;
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

core::Purchase makePurchase(int supplierId, long long totalCents, long long paidCents, bool addToStock)
{
    core::Purchase purchase;
    purchase.supplierId = supplierId;
    purchase.totalCents = totalCents;
    purchase.subtotalCents = totalCents;
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
    void no_stock_purchase();
    void pmp_calculation();
    void empty_items_fails();
    void rollback_on_bad_product();

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

    const data::PurchaseResult result = f.service.recordPurchase(purchase, lines);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.purchaseId > 0);

    const auto movements = f.stockMovements.findByProductId(productId);
    QCOMPARE(movements.size(), std::size_t(1));
    QCOMPARE(movements[0].delta, 100LL);
    QCOMPARE(movements[0].reason, QStringLiteral("purchase"));

    const auto product = f.products.findById(productId);
    QVERIFY(product.has_value());
    QCOMPARE(product->quantity, 100LL);
    QCOMPARE(product->costPriceCents, 5000LL);

    // Money owed and money paid are two rows, not one: the balance is
    // opening + sum(purchases) - sum(payments), so the invoice has to be
    // recorded and then offset, and a paid-in-full purchase nets to zero.
    QVector<long long> amounts;
    const auto txs = f.supplierTxs.findBySupplierId(supplierId);
    QCOMPARE(txs.size(), std::size_t(2));
    for (const core::SupplierTransaction& tx : txs) {
        amounts.push_back(tx.amountCents);
    }
    std::sort(amounts.begin(), amounts.end());
    QCOMPARE(amounts[0], -500000LL);
    QCOMPARE(amounts[1], 500000LL);
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

    // Nothing was paid, so the whole invoice is owed.
    const auto txs = f.supplierTxs.findBySupplierId(supplierId);
    QCOMPARE(txs.size(), std::size_t(1));
    QCOMPARE(txs[0].amountCents, 500000LL);
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

    const data::PurchaseResult result = f.service.recordPurchase(purchase, lines);
    QVERIFY2(result.ok, qPrintable(result.error));

    const auto movements = f.stockMovements.findByProductId(productId);
    QCOMPARE(movements.size(), std::size_t(1));
    QCOMPARE(movements[0].delta, 100LL);

    QVector<long long> amounts;
    const auto txs = f.supplierTxs.findBySupplierId(supplierId);
    QCOMPARE(txs.size(), std::size_t(2));
    for (const core::SupplierTransaction& tx : txs) {
        amounts.push_back(tx.amountCents);
    }
    std::sort(amounts.begin(), amounts.end());
    QCOMPARE(amounts[0], -400000LL);
    QCOMPARE(amounts[1], 1000000LL);
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

    // Nothing was received, so there is no stock side to owe for: no debt row,
    // and with nothing paid nothing to offset it either.
    QCOMPARE(f.supplierTxs.findBySupplierId(supplierId).size(), std::size_t(0));
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
    QCOMPARE(countRows(f.db, QStringLiteral("supplier_transactions")), 0);
}

QTEST_GUILESS_MAIN(PurchaseServiceTest)
#include "tst_purchase_service.moc"
