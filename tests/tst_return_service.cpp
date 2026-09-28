#include <QtTest/QtTest>

#include <QSqlQuery>
#include <QTemporaryDir>

#include "data/database.h"
#include "data/date_utils.h"
#include "data/product_repository.h"
#include "data/purchase_repository.h"
#include "data/stock_movement_repository.h"
#include "data/supplier_payment_repository.h"
#include "data/supplier_return_item_repository.h"
#include "data/supplier_return_repository.h"
#include "data/supplier_return_service.h"
#include "data/supplier_repository.h"

using namespace app;

namespace {

// One database per test: a return moves stock, so a shared file would let the
// order the tests run in decide what each one sees.
struct Fixture {
    explicit Fixture(const QString& path)
        : db(path)
        , returns(db)
        , returnItems(db)
        , suppliers(db)
        , purchases(db)
        , products(db)
        , stockMovements(db)
        , payments(db)
        , service(db, returns, returnItems, suppliers, purchases, products, stockMovements)
    {
    }

    data::Database db;
    data::SupplierReturnRepository returns;
    data::SupplierReturnItemRepository returnItems;
    data::SupplierRepository suppliers;
    data::PurchaseRepository purchases;
    data::ProductRepository products;
    data::StockMovementRepository stockMovements;
    data::SupplierPaymentRepository payments;
    data::SupplierReturnService service;
};

int addSupplier(data::SupplierRepository& suppliers, const QString& name)
{
    core::Supplier supplier;
    supplier.name = name;
    return suppliers.save(supplier);
}

// Stock level belongs to the stock-movement trigger, not to save(), which writes
// a new product at zero. A product that starts on the shelf therefore needs an
// opening movement, which is how real stock gets there.
int addProduct(data::ProductRepository& products, data::StockMovementRepository& stockMovements,
               const QString& name, long long quantity, long long costPriceCents = 0)
{
    core::Product product;
    product.name = name;
    product.costPriceCents = costPriceCents;
    product.salePriceCents = costPriceCents;
    const int productId = products.save(product);
    if (productId <= 0 || quantity == 0) {
        return productId;
    }

    core::StockMovement opening;
    opening.productId = productId;
    opening.delta = quantity;
    opening.reason = QStringLiteral("opening");
    opening.createdAt = QDateTime::currentDateTime();
    if (stockMovements.insert(opening) == 0) {
        return 0;
    }
    return productId;
}

int addPurchase(data::PurchaseRepository& purchases, int supplierId, long long totalCents)
{
    core::Purchase purchase;
    purchase.supplierId = supplierId;
    purchase.invoiceNumber = QStringLiteral("");
    purchase.note = QStringLiteral("");
    purchase.purchasedAt = data::nowIso();
    purchase.totalCents = totalCents;
    purchase.subtotalCents = totalCents;
    purchase.createdAt = data::nowIso();
    return purchases.insert(purchase);
}

core::SupplierReturnItem line(int productId, long long quantity, long long unitPriceCents)
{
    core::SupplierReturnItem item;
    item.productId = productId;
    item.quantity = quantity;
    item.unitPriceCents = unitPriceCents;
    item.totalCents = quantity * unitPriceCents;
    return item;
}

// Only the movements a return made, so the opening movement that put the product
// on the shelf is not counted as part of the return.
std::vector<core::StockMovement> returnMovements(const data::StockMovementRepository& stockMovements,
                                                 int productId)
{
    std::vector<core::StockMovement> filtered;
    for (const core::StockMovement& movement : stockMovements.findByProductId(productId)) {
        if (movement.reason == QLatin1String("supplier_return")) {
            filtered.push_back(movement);
        }
    }
    return filtered;
}

// Row counts read straight from the database, so a rollback is checked where it
// happened rather than through a repository that might be caching.
int countRows(const data::Database& db, const QString& table)
{
    QSqlQuery query(db.handle());
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table))) {
        return -1;
    }
    if (!query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

int countReturnMovements(const data::Database& db)
{
    QSqlQuery query(db.handle());
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM stock_movements WHERE reason = 'supplier_return'"))) {
        return -1;
    }
    if (!query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

} // namespace

class SupplierReturnServiceTest : public QObject {
    Q_OBJECT

private slots:
    void linked_return_simple();
    void general_return_simple();
    void balance_after_return();
    void linked_return_quantity_exceeds_stock_warns();
    void return_rejects_wrong_supplier();
    void return_rejects_empty_items();
    void return_rollback_on_item_failure();
    void return_does_not_change_average_cost();
};

void SupplierReturnServiceTest::linked_return_simple()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("linked_return_simple.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد مرتجع"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(f.purchases, supplierId, 50000);
    QVERIFY(purchaseId > 0);
    const int productId = addProduct(f.products, f.stockMovements, QStringLiteral("سكر"), 100);
    QVERIFY(productId > 0);

    const data::SupplierReturnResult result =
        f.service.recordLinkedReturn(supplierId, purchaseId, {line(productId, 20, 500)},
                                     data::nowIso(), QStringLiteral("مرتجع جزئي"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.returnId > 0);
    QVERIFY(result.warning.isEmpty());

    // The credit is what the lines add up to, taken from the lines rather than
    // from the caller.
    const auto stored = f.returns.findById(result.returnId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->amountCents, 10000LL);
    QVERIFY(stored->purchaseId.has_value());
    QCOMPARE(*stored->purchaseId, purchaseId);
    QCOMPARE(stored->removeFromStock, true);
    QCOMPARE(f.returnItems.findByReturn(result.returnId).size(), std::size_t(1));

    // The goods went back, so they left the shelves and left a movement saying so.
    const auto product = f.products.findById(productId);
    QVERIFY(product.has_value());
    QCOMPARE(product->quantity, 80LL);

    const auto movements = returnMovements(f.stockMovements, productId);
    QCOMPARE(movements.size(), std::size_t(1));
    QCOMPARE(movements[0].delta, -20LL);
    QCOMPARE(movements[0].reason, QStringLiteral("supplier_return"));
    QCOMPARE(movements[0].reference, QStringLiteral("Supplier Return #%1").arg(result.returnId));
}

void SupplierReturnServiceTest::general_return_simple()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("general_return_simple.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد مرتجع عام"));
    QVERIFY(supplierId > 0);
    const int productId = addProduct(f.products, f.stockMovements, QStringLiteral("زيت"), 10);
    QVERIFY(productId > 0);

    const data::SupplierReturnResult result =
        f.service.recordGeneralReturn(supplierId, 5000, data::nowIso(), QStringLiteral("مرتجع عام"));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.returnId > 0);

    const auto stored = f.returns.findById(result.returnId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->amountCents, 5000LL);
    // A return for its amount alone names no invoice and takes nothing off the
    // shelves, since it does not say which product went back.
    QVERIFY(!stored->purchaseId.has_value());
    QCOMPARE(stored->removeFromStock, false);
    QCOMPARE(f.returnItems.findByReturn(result.returnId).size(), std::size_t(0));
    QCOMPARE(returnMovements(f.stockMovements, productId).size(), std::size_t(0));

    // An amount that is not a credit is refused rather than stored.
    const data::SupplierReturnResult zero = f.service.recordGeneralReturn(supplierId, 0, data::nowIso(), QString());
    QVERIFY(!zero.ok);
    QCOMPARE(zero.error, QStringLiteral("amount must be positive"));
    const data::SupplierReturnResult negative =
        f.service.recordGeneralReturn(supplierId, -100, data::nowIso(), QString());
    QVERIFY(!negative.ok);
}

void SupplierReturnServiceTest::balance_after_return()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("balance_after_return.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد رصيد"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(f.purchases, supplierId, 50000);
    QVERIFY(purchaseId > 0);

    core::SupplierPayment payment;
    payment.supplierId = supplierId;
    payment.purchaseId = purchaseId;
    payment.amountCents = 20000;
    payment.paidAt = data::nowIso();
    payment.createdAt = data::nowIso();
    QVERIFY(f.payments.insert(payment) > 0);

    const data::SupplierReturnResult returned =
        f.service.recordGeneralReturn(supplierId, 5000, data::nowIso(), QString());
    QVERIFY2(returned.ok, qPrintable(returned.error));

    // 50000 invoiced, 20000 paid, 5000 given back: a return is money going the
    // other way, so it comes off what is owed just as a payment does.
    QCOMPARE(f.suppliers.balanceCentsFor(supplierId), 25000LL);
}

void SupplierReturnServiceTest::linked_return_quantity_exceeds_stock_warns()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("linked_return_quantity_exceeds_stock_warns.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد مخزون"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(f.purchases, supplierId, 50000);
    QVERIFY(purchaseId > 0);
    const int productId = addProduct(f.products, f.stockMovements, QStringLiteral("جبن"), 5);
    QVERIFY(productId > 0);

    // Returning more than was on the shelf is a stock count that was already
    // wrong. The goods did go back, so the return is recorded and the caller is
    // told, rather than refused and left invisible.
    const data::SupplierReturnResult result =
        f.service.recordLinkedReturn(supplierId, purchaseId, {line(productId, 10, 500)},
                                     data::nowIso(), QString());
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.returnId > 0);
    QVERIFY2(!result.warning.isEmpty(), "going below zero must be reported");
    QCOMPARE(result.warning, QStringLiteral("Stock is negative for جبن"));

    const auto product = f.products.findById(productId);
    QVERIFY(product.has_value());
    QCOMPARE(product->quantity, -5LL);
    QCOMPARE(f.returns.findById(result.returnId).has_value(), true);
}

void SupplierReturnServiceTest::return_rejects_wrong_supplier()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("return_rejects_wrong_supplier.sqlite")));
    const int supplierA = addSupplier(f.suppliers, QStringLiteral("مورد أ"));
    QVERIFY(supplierA > 0);
    const int supplierB = addSupplier(f.suppliers, QStringLiteral("مورد ب"));
    QVERIFY(supplierB > 0);
    const int purchaseId = addPurchase(f.purchases, supplierA, 50000);
    QVERIFY(purchaseId > 0);
    const int productId = addProduct(f.products, f.stockMovements, QStringLiteral("شاي"), 10);
    QVERIFY(productId > 0);

    // Crediting B for goods returned against A's invoice would settle the wrong
    // supplier's debt, so it is turned away before anything is written.
    const data::SupplierReturnResult result =
        f.service.recordLinkedReturn(supplierB, purchaseId, {line(productId, 1, 500)},
                                     data::nowIso(), QString());
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("purchase does not belong to supplier"));

    QCOMPARE(countRows(f.db, QStringLiteral("supplier_returns")), 0);
    QCOMPARE(f.products.findById(productId)->quantity, 10LL);
}

void SupplierReturnServiceTest::return_rejects_empty_items()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("return_rejects_empty_items.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد بلا بنود"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(f.purchases, supplierId, 50000);
    QVERIFY(purchaseId > 0);

    // A linked return with no lines would credit the supplier a total of zero
    // while still costing a stock movement to record and undo.
    const data::SupplierReturnResult result =
        f.service.recordLinkedReturn(supplierId, purchaseId, {}, data::nowIso(), QString());
    QVERIFY(!result.ok);
    QCOMPARE(result.error, QStringLiteral("return items are empty"));
    QCOMPARE(countRows(f.db, QStringLiteral("supplier_returns")), 0);
    QCOMPARE(countRows(f.db, QStringLiteral("supplier_return_items")), 0);

    // A line with no quantity, or no price, is a typo rather than a return.
    core::SupplierReturnItem noQuantity;
    noQuantity.quantity = 0;
    noQuantity.unitPriceCents = 500;
    noQuantity.totalCents = 0;
    const data::SupplierReturnResult zeroQuantity =
        f.service.recordLinkedReturn(supplierId, purchaseId, {noQuantity}, data::nowIso(), QString());
    QVERIFY(!zeroQuantity.ok);
    QCOMPARE(zeroQuantity.error, QStringLiteral("quantity must be positive"));

    core::SupplierReturnItem noPrice;
    noPrice.quantity = 5;
    noPrice.unitPriceCents = 0;
    noPrice.totalCents = 0;
    const data::SupplierReturnResult zeroPrice =
        f.service.recordLinkedReturn(supplierId, purchaseId, {noPrice}, data::nowIso(), QString());
    QVERIFY(!zeroPrice.ok);
    QCOMPARE(zeroPrice.error, QStringLiteral("unit price must be positive"));
}

void SupplierReturnServiceTest::return_rollback_on_item_failure()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("return_rollback_on_item_failure.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد فشل"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(f.purchases, supplierId, 50000);
    QVERIFY(purchaseId > 0);
    const int productId = addProduct(f.products, f.stockMovements, QStringLiteral("خبز"), 50);
    QVERIFY(productId > 0);

    // The first line is fine, the second names a product that does not exist.
    // The whole return has to disappear, including the first line and the credit
    // it had already written, or the supplier ends up credited for goods that
    // were never returned.
    const data::SupplierReturnResult result = f.service.recordLinkedReturn(
        supplierId, purchaseId, {line(productId, 5, 500), line(999999, 2, 500)}, data::nowIso(), QString());
    QVERIFY(!result.ok);

    QCOMPARE(countRows(f.db, QStringLiteral("supplier_returns")), 0);
    QCOMPARE(countRows(f.db, QStringLiteral("supplier_return_items")), 0);
    QCOMPARE(countReturnMovements(f.db), 0);
    QCOMPARE(f.products.findById(productId)->quantity, 50LL);

    // The connection is still usable, so the mistake can be corrected and the
    // return recorded for real.
    const data::SupplierReturnResult retry =
        f.service.recordLinkedReturn(supplierId, purchaseId, {line(productId, 5, 500)},
                                     data::nowIso(), QString());
    QVERIFY2(retry.ok, qPrintable(retry.error));
    QCOMPARE(f.products.findById(productId)->quantity, 45LL);
}

void SupplierReturnServiceTest::return_does_not_change_average_cost()
{
    QTemporaryDir m_dir;
    Fixture f(m_dir.filePath(QStringLiteral("return_does_not_change_average_cost.sqlite")));
    const int supplierId = addSupplier(f.suppliers, QStringLiteral("مورد تكلفة"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(f.purchases, supplierId, 50000);
    QVERIFY(purchaseId > 0);
    const int productId = addProduct(f.products, f.stockMovements, QStringLiteral("لبن"), 100, 900);
    QVERIFY(productId > 0);

    const data::SupplierReturnResult result =
        f.service.recordLinkedReturn(supplierId, purchaseId, {line(productId, 20, 500)},
                                     data::nowIso(), QString());
    QVERIFY2(result.ok, qPrintable(result.error));

    // The goods came back at the price they were bought for, so taking them off
    // the shelf changes how much is there and not what it cost. Recomputing the
    // average here would quietly reprice everything still in stock.
    const auto product = f.products.findById(productId);
    QVERIFY(product.has_value());
    QCOMPARE(product->quantity, 80LL);
    QCOMPARE(product->costPriceCents, 900LL);
}

QTEST_GUILESS_MAIN(SupplierReturnServiceTest)
#include "tst_return_service.moc"
