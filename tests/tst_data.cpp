#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QSqlQuery>

#include "database.h"
#include "product_repository.h"
#include "sale_repository.h"
#include "sale_item_repository.h"
#include "customer_repository.h"
#include "customer_transaction_repository.h"
#include "customer_transaction_item_repository.h"
#include "supplier_repository.h"
#include "supplier_transaction_repository.h"
#include "payment_repository.h"
#include "expense_repository.h"
#include "owner_drawing_repository.h"
#include "stock_movement_repository.h"
#include "cash_session_repository.h"
#include "cash_movement_repository.h"
#include "user_repository.h"
#include "device_repository.h"
#include "audit_log_repository.h"
#include "zakat_setting_repository.h"
#include "setting_repository.h"

using namespace app;

class DataLayerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void schemaContainsAllTables();
    void productSaveAndFind();
    void stockInvariantIsDerived();
    void saleInsertAndItems();
    void customerTransactionAndItems();
    void paymentInsert();
    void supplierTransactionInsert();
    void expenseAndDrawingInsert();
    void cashSessionLifecycle();
    void settingsRoundTrip();
    void appendOnlyGuard();

private:
    QTemporaryDir m_dir;
    std::unique_ptr<data::Database> m_db;
};

void DataLayerTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_db = std::make_unique<data::Database>(m_dir.filePath(QStringLiteral("test.sqlite")));
}

void DataLayerTest::schemaContainsAllTables()
{
    const QStringList expected = {
        QStringLiteral("products"), QStringLiteral("sales"), QStringLiteral("sale_items"),
        QStringLiteral("customers"), QStringLiteral("customer_transactions"),
        QStringLiteral("customer_transaction_items"), QStringLiteral("suppliers"),
        QStringLiteral("supplier_transactions"), QStringLiteral("payments"),
        QStringLiteral("expenses"), QStringLiteral("owner_drawings"),
        QStringLiteral("stock_movements"), QStringLiteral("cash_sessions"),
        QStringLiteral("cash_movements"), QStringLiteral("users"), QStringLiteral("devices"),
        QStringLiteral("audit_log"), QStringLiteral("zakat_settings"), QStringLiteral("settings"),
        QStringLiteral("sync_outbox"), QStringLiteral("applied_ops"), QStringLiteral("sync_sequence"),
    };

    QSqlQuery query(m_db->handle());
    QVERIFY(query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'")));

    QStringList actual;
    while (query.next()) {
        actual << query.value(0).toString();
    }

    for (const QString& table : expected) {
        QVERIFY2(actual.contains(table), qPrintable(QStringLiteral("missing table: %1").arg(table)));
    }

    QSqlQuery trigger(m_db->handle());
    QVERIFY(trigger.exec(
        QStringLiteral("SELECT name FROM sqlite_master WHERE type='trigger' AND name='trg_stock_after_insert'")));
    QVERIFY(trigger.next());
}

void DataLayerTest::productSaveAndFind()
{
    data::ProductRepository repo(*m_db);

    core::Product product;
    product.barcode = QStringLiteral("6130000000001");
    product.name = QStringLiteral("حليب");
    product.costPriceCents = 9500;
    product.salePriceCents = 12000;
    product.unit = QStringLiteral("وحدة");
    product.packageSize = 1;
    const int id = repo.save(product);
    QVERIFY(id > 0);

    const auto found = repo.findByBarcode(QStringLiteral("6130000000001"));
    QVERIFY(found.has_value());
    QCOMPARE(found->id, id);
    QCOMPARE(found->name, QStringLiteral("حليب"));
    QCOMPARE(found->salePriceCents, 12000LL);
    QCOMPARE(found->quantity, 0LL);

    QVERIFY(!repo.findByBarcode(QStringLiteral("9999999999999")).has_value());
}

void DataLayerTest::stockInvariantIsDerived()
{
    data::ProductRepository productRepo(*m_db);
    data::StockMovementRepository movementRepo(*m_db);

    core::Product product;
    product.barcode = QStringLiteral("6130000000002");
    product.name = QStringLiteral("سكر");
    product.costPriceCents = 8500;
    product.salePriceCents = 10000;
    const int id = productRepo.save(product);
    QVERIFY(id > 0);

    productRepo.adjustStock(id, 100, QStringLiteral("purchase"));
    productRepo.adjustStock(id, -3, QStringLiteral("sale"));

    const auto reloaded = productRepo.findById(id);
    QVERIFY(reloaded.has_value());
    QCOMPARE(reloaded->quantity, 97LL);
    QCOMPARE(movementRepo.sumByProductId(id), 97LL);
    QVERIFY(m_db->verifyStockConsistency());

    core::StockMovement movement;
    movement.productId = id;
    movement.delta = -7;
    movement.reason = QStringLiteral("manual_adjustment");
    movementRepo.insert(movement);

    const auto after = productRepo.findById(id);
    QVERIFY(after.has_value());
    QCOMPARE(after->quantity, 90LL);
    QVERIFY(m_db->verifyStockConsistency());
}

void DataLayerTest::saleInsertAndItems()
{
    data::ProductRepository productRepo(*m_db);
    data::SaleRepository saleRepo(*m_db);
    data::SaleItemRepository itemRepo(*m_db);

    core::Product product;
    product.barcode = QStringLiteral("6130000000003");
    product.name = QStringLiteral("قهوة");
    product.costPriceCents = 3000;
    product.salePriceCents = 5000;
    const int productId = productRepo.save(product);

    core::Sale sale;
    sale.totalCents = 10000;
    sale.deviceId = QStringLiteral("PHONE_01");
    const int saleId = saleRepo.insert(sale);
    QVERIFY(saleId > 0);

    core::SaleItem item;
    item.saleId = saleId;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 5000;
    item.unitCostCents = 3000;
    const int itemId = itemRepo.insert(item);
    QVERIFY(itemId > 0);

    const auto items = itemRepo.findBySaleId(saleId);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].quantity, 2LL);
    QCOMPARE(items[0].unitCostCents, 3000LL);

    const auto foundSale = saleRepo.findById(saleId);
    QVERIFY(foundSale.has_value());
    QCOMPARE(foundSale->totalCents, 10000LL);
}

void DataLayerTest::customerTransactionAndItems()
{
    data::CustomerRepository customerRepo(*m_db);
    data::CustomerTransactionRepository txRepo(*m_db);
    data::CustomerTransactionItemRepository itemRepo(*m_db);

    core::Customer customer;
    customer.name = QStringLiteral("محمد");
    const int customerId = customerRepo.save(customer);
    QVERIFY(customerId > 0);

    core::CustomerTransaction tx;
    tx.customerId = customerId;
    tx.amountCents = 70000;
    const int txId = txRepo.insert(tx);
    QVERIFY(txId > 0);

    core::CustomerTransactionItem item;
    item.customerTransactionId = txId;
    item.productId = 1;
    item.quantity = 1;
    item.unitPriceCents = 70000;
    item.unitCostCents = 50000;
    itemRepo.insert(item);

    const auto items = itemRepo.findByTransactionId(txId);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].unitCostCents, 50000LL);

    const auto txs = txRepo.findByCustomerId(customerId);
    QCOMPARE(txs.size(), 1);
    QCOMPARE(txs[0].amountCents, 70000LL);
}

void DataLayerTest::paymentInsert()
{
    data::CustomerRepository customerRepo(*m_db);
    data::PaymentRepository paymentRepo(*m_db);

    core::Customer customer;
    customer.name = QStringLiteral("خالد");
    const int customerId = customerRepo.save(customer);

    core::Payment payment;
    payment.customerId = customerId;
    payment.amountCents = 50000;
    const int paymentId = paymentRepo.insert(payment);
    QVERIFY(paymentId > 0);

    const auto payments = paymentRepo.findByCustomerId(customerId);
    QCOMPARE(payments.size(), 1);
    QCOMPARE(payments[0].amountCents, 50000LL);
}

void DataLayerTest::supplierTransactionInsert()
{
    data::SupplierRepository supplierRepo(*m_db);
    data::SupplierTransactionRepository txRepo(*m_db);

    core::Supplier supplier;
    supplier.name = QStringLiteral("المورد سعيد");
    const int supplierId = supplierRepo.save(supplier);
    QVERIFY(supplierId > 0);

    core::SupplierTransaction tx;
    tx.supplierId = supplierId;
    tx.amountCents = 250000;
    tx.note = QStringLiteral("فاتورة");
    const int txId = txRepo.insert(tx);
    QVERIFY(txId > 0);

    const auto txs = txRepo.findBySupplierId(supplierId);
    QCOMPARE(txs.size(), 1);
    QCOMPARE(txs[0].amountCents, 250000LL);
}

void DataLayerTest::expenseAndDrawingInsert()
{
    data::ExpenseRepository expenseRepo(*m_db);
    data::OwnerDrawingRepository drawingRepo(*m_db);

    core::Expense expense;
    expense.label = QStringLiteral("كهرباء");
    expense.amountCents = 5000;
    const int expenseId = expenseRepo.insert(expense);
    QVERIFY(expenseId > 0);

    core::OwnerDrawing drawing;
    drawing.note = QStringLiteral("سحب شخصي");
    drawing.amountCents = 20000;
    const int drawingId = drawingRepo.insert(drawing);
    QVERIFY(drawingId > 0);
}

void DataLayerTest::cashSessionLifecycle()
{
    data::CashSessionRepository sessionRepo(*m_db);
    data::CashMovementRepository movementRepo(*m_db);

    const int sessionId = sessionRepo.open(10000);
    QVERIFY(sessionId > 0);

    const auto open = sessionRepo.findOpen();
    QVERIFY(open.has_value());
    QCOMPARE(open->id, sessionId);
    QCOMPARE(open->status, QStringLiteral("open"));

    core::CashMovement m1;
    m1.sessionId = sessionId;
    m1.type = QStringLiteral("sale");
    m1.amountCents = 25000;
    movementRepo.insert(m1);

    core::CashMovement m2;
    m2.sessionId = sessionId;
    m2.type = QStringLiteral("customer_payment");
    m2.amountCents = 50000;
    movementRepo.insert(m2);

    QCOMPARE(movementRepo.sumBySessionId(sessionId), 75000LL);

    QVERIFY(sessionRepo.close(sessionId, 85000, 85000, 0));

    QVERIFY(!sessionRepo.findOpen().has_value());
    const auto closed = sessionRepo.findById(sessionId);
    QVERIFY(closed.has_value());
    QCOMPARE(closed->status, QStringLiteral("closed"));
    QCOMPARE(closed->closingCountedCents, 85000LL);
    QCOMPARE(closed->varianceCents, 0LL);
}

void DataLayerTest::settingsRoundTrip()
{
    data::SettingRepository settingRepo(*m_db);
    data::ZakatSettingRepository zakatRepo(*m_db);

    settingRepo.set(QStringLiteral("batch_size"), QStringLiteral("50"));
    QCOMPARE(settingRepo.value(QStringLiteral("batch_size")), std::optional<QString>(QStringLiteral("50")));
    QVERIFY(!settingRepo.value(QStringLiteral("missing")).has_value());

    zakatRepo.set(QStringLiteral("nisab_cents"), QStringLiteral("1000000"));
    const auto nisab = zakatRepo.findByKey(QStringLiteral("nisab_cents"));
    QVERIFY(nisab.has_value());
    QCOMPARE(nisab->value, QStringLiteral("1000000"));
}

void DataLayerTest::appendOnlyGuard()
{
    // Append-only discipline: insert a duplicate payment reversal referencing the original.
    data::CustomerRepository customerRepo(*m_db);
    data::PaymentRepository paymentRepo(*m_db);

    core::Customer customer;
    customer.name = QStringLiteral("عبد الرحمن");
    const int customerId = customerRepo.save(customer);

    core::Payment payment;
    payment.customerId = customerId;
    payment.amountCents = 10000;
    const int paymentId = paymentRepo.insert(payment);

    paymentRepo.reverse(paymentId, 10000, QStringLiteral("reversal"));

    const auto payments = paymentRepo.findByCustomerId(customerId);
    QCOMPARE(payments.size(), 2);
    QCOMPARE(payments[1].amountCents, -10000LL);
    QCOMPARE(payments[1].reversedId, paymentId);
}

QTEST_GUILESS_MAIN(DataLayerTest)
#include "tst_data.moc"