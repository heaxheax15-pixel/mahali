#include <QtTest/QtTest>

#include <limits>
#include <optional>
#include <QTemporaryDir>
#include <QFile>
#include <QSqlQuery>

#include "database.h"
#include "applied_op_repository.h"
#include "product_repository.h"
#include "sale_repository.h"
#include "sale_item_repository.h"
#include "sale_service.h"
#include "customer_repository.h"
#include "customer_transaction_repository.h"
#include "customer_transaction_item_repository.h"
#include "supplier_repository.h"
#include "supplier_payment_repository.h"
#include "purchase_repository.h"
#include "payment_repository.h"
#include "expense_repository.h"
#include "owner_drawing_repository.h"
#include "stock_movement_repository.h"
#include "cash_session_repository.h"
#include "cash_movement_repository.h"
#include "user_repository.h"
#include "admin_secret_repository.h"
#include "device_repository.h"
#include "audit_log_repository.h"
#include "zakat_setting_repository.h"
#include "setting_repository.h"
#include "core/i18n.h"
#include "sync_outbox_repository.h"
#include "device_ledger_service.h"
#include "device_identity.h"
#include "date_utils.h"
#include "sale_rules.h"

using namespace app;

// The window the reversal tests read back. One hour either side of startup,
// wide enough to cover the whole run: the suite shares a single database, so a
// test cannot assume it is looking at an empty ledger and has to count relative
// to what is already there.
const QDateTime g_reversalWindowFrom = QDateTime::currentDateTime().addSecs(-3600);
const QDateTime g_reversalWindowTo = QDateTime::currentDateTime().addSecs(3600);

class DataLayerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void schemaContainsAllTables();
    void productSaveAndFind();
    void quick_items_roundtrip();
    void sold_by_weight_persists();
    void legacy_products_migration();
    void stockInvariantIsDerived();
    void stock_movement_reference_roundtrip();
    void saleInsertAndItems();
    void customerTransactionAndItems();
    void paymentInsert();
    void expenseAndDrawingInsert();
    void reverse_expense_returns_true_on_success();
    void reverse_expense_returns_false_on_missing();
    void reverse_expense_refuses_a_second_reversal();
    void reverse_owner_drawing_returns_false_on_missing();
    void reverse_owner_drawing_returns_true_on_success();
    void cash_session_repo_rejects_zero_open();
    void cash_session_repo_rejects_negative_close();
    void close_returns_false_for_missing_session();
    void open_refuses_when_session_already_open();
    void close_is_idempotent();
    void schema_has_single_open_index();
    void open_twice_via_repo_fails();
    void legacy_db_with_two_open_sessions_still_opens();
    void device_ledger_rejects_empty_device_id();
    void cashSessionLifecycle();
    void settingsRoundTrip();
    void appendOnlyGuard();
    void journalModeMatchesDatabaseRole();
    void pruneOnlyExpiredAcknowledgedOutboxRows();
    void appliedOpsJournalPrunesBoundedly();
    void user_pin_roundtrip();
    void admin_master_roundtrip();
    void language_setting_persists();
    void repository_error_is_recorded();
    void last_error_clears_on_success();
    void reverseSale_returns_error_on_missing_sale();
    void payment_reverse_returns_false_on_missing();
    void reverseSale_refuses_double_reversal();
    void overflow_guard();
    void overflow_guard_cogs();
    void supplier_new_fields_roundtrip();
    void supplier_list_active_excludes_inactive();
    void supplier_balance_uses_opening_and_transactions();
    void supplier_payment_roundtrip();
    void supplier_payment_find_by_supplier();
    void supplier_payment_find_by_purchase();
    void supplier_payment_schema_exists();

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
        QStringLiteral("purchases"), QStringLiteral("purchase_items"),
        QStringLiteral("supplier_payments"), QStringLiteral("payments"),
        QStringLiteral("expenses"), QStringLiteral("owner_drawings"),
        QStringLiteral("stock_movements"), QStringLiteral("cash_sessions"),
        QStringLiteral("cash_movements"), QStringLiteral("users"), QStringLiteral("devices"),
        QStringLiteral("audit_log"), QStringLiteral("zakat_settings"), QStringLiteral("settings"),
        QStringLiteral("sync_outbox"), QStringLiteral("applied_ops"), QStringLiteral("sync_sequence"),
        QStringLiteral("admin_secrets"),
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

void DataLayerTest::quick_items_roundtrip()
{
    data::ProductRepository repo(*m_db);

    core::Product scanned;
    scanned.barcode = QStringLiteral("123");
    scanned.name = QStringLiteral("حليب");
    QVERIFY(repo.save(scanned) > 0);

    // Quick items carry no barcode, stored as NULL: UNIQUE would reject a
    // second blank string but allows any number of NULLs.
    core::Product bread;
    bread.barcode = QString();  // null -> "no barcode"
    bread.name = QStringLiteral("خبز");
    const int breadId = repo.save(bread);
    QVERIFY(breadId > 0);

    core::Product water;
    water.barcode = QString();
    water.name = QStringLiteral("ماء");
    const int waterId = repo.save(water);
    QVERIFY(waterId > 0);

    const auto quick = repo.findQuickItems();
    QCOMPARE(quick.size(), std::size_t(2));
    for (const auto& product : quick) {
        QVERIFY(product.barcode.isNull());
        QVERIFY(product.active);
    }

    // The barcoded product is not a quick item.
    for (const auto& product : quick) {
        QVERIFY(product.id != scanned.id);
    }

    const auto byName = repo.findQuickItemsByName(QStringLiteral("خبز"));
    QCOMPARE(byName.size(), std::size_t(1));
    QCOMPARE(byName.front().id, breadId);
    QCOMPARE(byName.front().name, QStringLiteral("خبز"));

    // A name that matches no quick item returns nothing.
    QVERIFY(repo.findQuickItemsByName(QStringLiteral("حليب")).empty());

    // A LIKE wildcard typed by the user is matched literally, not as a pattern.
    QVERIFY(repo.findQuickItemsByName(QStringLiteral("%")).empty());

    // Inactive quick items drop out of the listing.
    repo.setActive(waterId, false);
    QCOMPARE(repo.findQuickItems().size(), std::size_t(1));
    QCOMPARE(repo.findQuickItems().front().id, breadId);
}

void DataLayerTest::sold_by_weight_persists()
{
    data::ProductRepository repo(*m_db);

    core::Product product;
    product.barcode = QStringLiteral("7788990011223");
    product.name = QStringLiteral("لحم");
    product.soldByWeight = true;
    const int id = repo.save(product);
    QVERIFY(id > 0);

    const auto found = repo.findById(id);
    QVERIFY(found.has_value());
    QVERIFY(found->soldByWeight);

    repo.setSoldByWeight(id, false);
    const auto cleared = repo.findById(id);
    QVERIFY(cleared.has_value());
    QVERIFY(!cleared->soldByWeight);

    repo.setSoldByWeight(id, true);
    const auto restored = repo.findById(id);
    QVERIFY(restored.has_value());
    QVERIFY(restored->soldByWeight);

    // Defaults to false for a product that never set it.
    core::Product plain;
    plain.barcode = QStringLiteral("5566778899001");
    plain.name = QStringLiteral("أرز");
    const int plainId = repo.save(plain);
    QVERIFY(plainId > 0);
    const auto plainFound = repo.findById(plainId);
    QVERIFY(plainFound.has_value());
    QVERIFY(!plainFound->soldByWeight);
}

// Every other test starts from a fresh schema, where products is already
// nullable and only an ADD COLUMN is needed. This one seeds a database in the
// pre-feature shape (barcode NOT NULL, no sold_by_weight) to cover the table
// rebuild, the trigger swap and the row copy.
void DataLayerTest::legacy_products_migration()
{
    const QString path = m_dir.filePath(QStringLiteral("legacy.sqlite"));
    const QString connection = QStringLiteral("legacy_seed");
    {
        QSqlDatabase seed = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        seed.setDatabaseName(path);
        QVERIFY(seed.open());
        {
            QSqlQuery create(seed);
            // The old products table: barcode was mandatory.
            QVERIFY(create.exec(QStringLiteral(
                "CREATE TABLE products ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "barcode TEXT UNIQUE NOT NULL,"
                "name TEXT NOT NULL,"
                "cost_price_cents INTEGER NOT NULL DEFAULT 0,"
                "sale_price_cents INTEGER NOT NULL DEFAULT 0,"
                "quantity INTEGER NOT NULL DEFAULT 0,"
                "unit TEXT NOT NULL DEFAULT '',"
                "package_size INTEGER NOT NULL DEFAULT 1,"
                "active INTEGER NOT NULL DEFAULT 1)")));
            // A child table holding a foreign key, and the trigger that is
            // declared on stock_movements but writes to products.
            QVERIFY(create.exec(QStringLiteral(
                "CREATE TABLE stock_movements ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "product_id INTEGER NOT NULL REFERENCES products(id),"
                "delta INTEGER NOT NULL,"
                "reason TEXT NOT NULL,"
                "created_at TEXT NOT NULL,"
                "reversed_id INTEGER NOT NULL DEFAULT 0)")));
            QVERIFY(create.exec(QStringLiteral(
                "CREATE TRIGGER trg_stock_after_insert AFTER INSERT ON stock_movements BEGIN "
                "UPDATE products SET quantity = quantity + NEW.delta WHERE id = NEW.product_id; END")));
            QVERIFY(create.exec(QStringLiteral(
                "INSERT INTO products (id, barcode, name, quantity) VALUES (1, '111', 'قديم', 4)")));
            QVERIFY(create.exec(QStringLiteral(
                "INSERT INTO stock_movements (product_id, delta, reason, created_at) "
                "VALUES (1, 3, 'opening', '2026-01-01')")));
        }
        seed.close();
    }
    QSqlDatabase::removeDatabase(connection);
    QVERIFY(QFile::exists(path));

    data::Database db(path);
    QVERIFY2(db.lastError().isEmpty(), qPrintable(db.lastError()));

    // The swap added the column and made barcode nullable, keeping the data.
    bool hasWeightColumn = false;
    {
        QSqlQuery info(db.handle());
        QVERIFY(info.exec(QStringLiteral("PRAGMA table_info(products)")));
        while (info.next()) {
            if (info.value(1).toString() == QStringLiteral("barcode")) {
                QCOMPARE(info.value(3).toInt(), 0);
            }
            if (info.value(1).toString() == QStringLiteral("sold_by_weight")) {
                hasWeightColumn = true;
                QCOMPARE(info.value(3).toInt(), 1);
            }
        }
    }
    QVERIFY(hasWeightColumn);

    data::ProductRepository repo(db);
    const auto migrated = repo.findById(1);
    QVERIFY(migrated.has_value());
    QCOMPARE(migrated->barcode, QStringLiteral("111"));
    QCOMPARE(migrated->name, QStringLiteral("قديم"));
    QVERIFY(!migrated->soldByWeight);

    // The trigger survived the swap and still drives the quantity.
    repo.adjustStock(1, 2, QStringLiteral("restock"));
    const auto restocked = repo.findById(1);
    QVERIFY(restocked.has_value());
    QCOMPARE(restocked->quantity, 9LL);

    // Nothing dangles: the child table still resolves its foreign key.
    {
        QSqlQuery check(db.handle());
        QVERIFY(check.exec(QStringLiteral("PRAGMA foreign_key_check")));
        QVERIFY(!check.next());
    }

    // Quick items are finally insertable on the migrated database.
    core::Product first;
    first.barcode = QString();
    first.name = QStringLiteral("خبز");
    const int firstId = repo.save(first);
    QVERIFY(firstId > 0);
    core::Product second;
    second.barcode = QString();
    second.name = QStringLiteral("ماء");
    const int secondId = repo.save(second);
    QVERIFY(secondId > 0);
    QCOMPARE(repo.findQuickItems().size(), std::size_t(2));

    // Re-opening is idempotent: the migration sees the finished schema and
    // leaves the data alone.
    {
        data::Database reopened(path);
        QVERIFY2(reopened.lastError().isEmpty(), qPrintable(reopened.lastError()));
        data::ProductRepository reopenedRepo(reopened);
        const auto again = reopenedRepo.findById(1);
        QVERIFY(again.has_value());
        QCOMPARE(again->quantity, 9LL);
        QCOMPARE(reopenedRepo.findQuickItems().size(), std::size_t(2));
    }
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

void DataLayerTest::stock_movement_reference_roundtrip()
{
    data::ProductRepository productRepo(*m_db);
    data::StockMovementRepository movementRepo(*m_db);

    core::Product product;
    product.barcode = QStringLiteral("6130000000099");
    product.name = QStringLiteral("ماء معدني");
    product.costPriceCents = 1000;
    product.salePriceCents = 1500;
    const int productId = productRepo.save(product);
    QVERIFY(productId > 0);

    core::StockMovement movement;
    movement.productId = productId;
    movement.delta = 12;
    movement.reason = QStringLiteral("purchase");
    movement.reference = QStringLiteral("Test #1");
    const int movementId = movementRepo.insert(movement);
    QVERIFY(movementId > 0);

    // The reference is what tells a reader whether the stock came from a
    // purchase, a sale or a correction, so it has to survive the round trip.
    const auto stored = movementRepo.findById(movementId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->reference, QStringLiteral("Test #1"));
    QCOMPARE(stored->reason, QStringLiteral("purchase"));
    QCOMPARE(stored->delta, 12LL);

    const auto byProduct = movementRepo.findByProductId(productId);
    QCOMPARE(byProduct.size(), std::size_t(1));
    QCOMPARE(byProduct[0].reference, QStringLiteral("Test #1"));

    // A movement with nothing to point at still reads back as an empty string,
    // not a null one, so the UI never has to guard the label.
    core::StockMovement bare;
    bare.productId = productId;
    bare.delta = -2;
    bare.reason = QStringLiteral("manual_adjustment");
    const int bareId = movementRepo.insert(bare);
    QVERIFY(bareId > 0);
    const auto bareStored = movementRepo.findById(bareId);
    QVERIFY(bareStored.has_value());
    QVERIFY(bareStored->reference.isEmpty());
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

void DataLayerTest::reverse_expense_returns_true_on_success()
{
    data::ExpenseRepository expenseRepo(*m_db);

    core::Expense expense;
    expense.label = QStringLiteral("كهرباء");
    expense.amountCents = 5000;
    const int expenseId = expenseRepo.insert(expense);
    QVERIFY(expenseId > 0);

    // The table is shared with the rest of the suite, so this counts relative to
    // what is already there rather than assuming an empty ledger.
    const int before = expenseRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo).size();

    // The whole point of the bool: a caller that ignored it had no way to tell a
    // written reversal from a silently dropped one.
    QVERIFY(expenseRepo.reverse(expenseId));

    const auto all = expenseRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo);
    QCOMPARE(all.size(), before + 1);

    // The mirrored row: same label, opposite sign, pointing back at the original.
    const core::Expense* mirrored = nullptr;
    for (const core::Expense& row : all) {
        if (row.reversedId == expenseId) {
            mirrored = &row;
        }
    }
    QVERIFY2(mirrored != nullptr, "no mirrored row was written");
    QCOMPARE(mirrored->amountCents, -5000LL);
    QCOMPARE(mirrored->label, expense.label);

    // The original keeps its own amount: a reversal adds a row, it does not
    // rewrite history.
    const auto original = expenseRepo.findById(expenseId);
    QVERIFY(original.has_value());
    QCOMPARE(original->amountCents, 5000LL);
}

void DataLayerTest::reverse_expense_returns_false_on_missing()
{
    data::ExpenseRepository expenseRepo(*m_db);

    const int before = expenseRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo).size();
    // 99999 is not a row anyone inserted: the old void signature returned here
    // with no signal at all, and the caller went on to report success.
    QVERIFY(!expenseRepo.reverse(99999));
    QCOMPARE(expenseRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo).size(), before);
}

void DataLayerTest::reverse_expense_refuses_a_second_reversal()
{
    data::ExpenseRepository expenseRepo(*m_db);

    core::Expense expense;
    expense.label = QStringLiteral("ماء");
    expense.amountCents = 3000;
    const int expenseId = expenseRepo.insert(expense);
    QVERIFY(expenseId > 0);

    const int before = expenseRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo).size();
    QVERIFY(expenseRepo.reverse(expenseId));
    QCOMPARE(expenseRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo).size(), before + 1);

    // Reversing the same entry twice used to succeed and write a second mirrored
    // row, which returned the money to the till twice.
    QVERIFY(!expenseRepo.reverse(expenseId));
    QCOMPARE(expenseRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo).size(), before + 1);

    // And the mirrored row cannot itself be reversed: that would cancel the
    // cancellation.
    const auto all = expenseRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo);
    int mirroredId = 0;
    for (const core::Expense& row : all) {
        if (row.reversedId == expenseId) {
            mirroredId = row.id;
        }
    }
    QVERIFY(mirroredId != 0);
    QVERIFY(!expenseRepo.reverse(mirroredId));
    QCOMPARE(expenseRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo).size(), before + 1);
}

void DataLayerTest::reverse_owner_drawing_returns_true_on_success()
{
    data::OwnerDrawingRepository drawingRepo(*m_db);

    core::OwnerDrawing drawing;
    drawing.note = QStringLiteral("سحب شخصي");
    drawing.amountCents = 20000;
    const int drawingId = drawingRepo.insert(drawing);
    QVERIFY(drawingId > 0);

    const int before = drawingRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo).size();
    QVERIFY(drawingRepo.reverse(drawingId));

    const auto all = drawingRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo);
    QCOMPARE(all.size(), before + 1);

    const core::OwnerDrawing* mirrored = nullptr;
    for (const core::OwnerDrawing& row : all) {
        if (row.reversedId == drawingId) {
            mirrored = &row;
        }
    }
    QVERIFY2(mirrored != nullptr, "no mirrored row was written");
    QCOMPARE(mirrored->amountCents, -20000LL);
    QCOMPARE(mirrored->note, drawing.note);
}

void DataLayerTest::reverse_owner_drawing_returns_false_on_missing()
{
    data::OwnerDrawingRepository drawingRepo(*m_db);

    const int before = drawingRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo).size();
    QVERIFY(!drawingRepo.reverse(99999));
    QCOMPARE(drawingRepo.findBetween(g_reversalWindowFrom, g_reversalWindowTo).size(), before);
}

// The repository exposes no row count, and the whole point of these tests is
// that a refused call writes nothing, so the counts are read straight from the
// table. countOpenCashSessions mirrors the status = 'open' condition that
// findOpen() uses.
int countCashSessions(const data::Database& db)
{
    QSqlQuery query(db.handle());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM cash_sessions"));
    if (!query.exec() || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

int countOpenCashSessions(const data::Database& db)
{
    QSqlQuery query(db.handle());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM cash_sessions WHERE status = 'open'"));
    if (!query.exec() || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

// PRAGMA index_list reports one row per index: seq, name, unique, origin, partial.
bool cashSessionIndexExists(const data::Database& db, const QString& name, bool* isUnique = nullptr,
                            bool* isPartial = nullptr)
{
    QSqlQuery query(db.handle());
    if (!query.exec(QStringLiteral("PRAGMA index_list(cash_sessions)"))) {
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() != name) {
            continue;
        }
        if (isUnique) {
            *isUnique = query.value(2).toInt() == 1;
        }
        if (isPartial) {
            *isPartial = query.value(4).toInt() == 1;
        }
        return true;
    }
    return false;
}

void DataLayerTest::cash_session_repo_rejects_zero_open()
{
    data::CashSessionRepository sessionRepo(*m_db);

    const int before = countCashSessions(*m_db);
    QVERIFY(before >= 0);

    // Zero is a valid long long and a plausible typo at every call site, so the
    // repository has to be the one that says no. It used to write the row.
    QCOMPARE(sessionRepo.open(0), 0);
    QCOMPARE(countCashSessions(*m_db), before);

    // Negative, on the same reasoning.
    QCOMPARE(sessionRepo.open(-5000), 0);
    QCOMPARE(countCashSessions(*m_db), before);

    // And a real float still opens, so the guard is not simply refusing
    // everything.
    const int sessionId = sessionRepo.open(5000);
    QVERIFY(sessionId > 0);
    QCOMPARE(countCashSessions(*m_db), before + 1);
    const auto opened = sessionRepo.findById(sessionId);
    QVERIFY(opened.has_value());
    QCOMPARE(opened->id, sessionId);
    QCOMPARE(opened->status, QStringLiteral("open"));
    QCOMPARE(opened->openingFloatCents, 5000LL);
    QVERIFY(sessionRepo.findOpen().has_value());

    // The suite shares one database, so hand it back the way it was found:
    // leaving a session open here would make every later findOpen() lie.
    QVERIFY(sessionRepo.close(sessionId, 5000, 5000, 0));
    QVERIFY(!sessionRepo.findOpen().has_value());
}

void DataLayerTest::cash_session_repo_rejects_negative_close()
{
    data::CashSessionRepository sessionRepo(*m_db);

    const int sessionId = sessionRepo.open(5000);
    QVERIFY(sessionId > 0);
    QVERIFY(sessionRepo.findOpen().has_value());

    // Nothing was counted, so the day must not end. This used to write the row
    // and book a 5000 deficit against the till.
    QVERIFY(!sessionRepo.close(sessionId, -100, 5000, -5100));
    QVERIFY(sessionRepo.findOpen().has_value());
    const auto stillOpen = sessionRepo.findById(sessionId);
    QVERIFY(stillOpen.has_value());
    QCOMPARE(stillOpen->status, QStringLiteral("open"));

    // Zero is refused for the same reason.
    QVERIFY(!sessionRepo.close(sessionId, 0, 5000, -5000));
    QVERIFY(sessionRepo.findOpen().has_value());

    // A genuine shortfall is still allowed through: a negative variance is a
    // real result, not an invalid amount, and refusing it would make a short
    // till impossible to close at all.
    QVERIFY(sessionRepo.close(sessionId, 100, 5000, -4900));
    QVERIFY(!sessionRepo.findOpen().has_value());
    const auto closed = sessionRepo.findById(sessionId);
    QVERIFY(closed.has_value());
    QCOMPARE(closed->status, QStringLiteral("closed"));
    QCOMPARE(closed->varianceCents, -4900LL);
}

void DataLayerTest::close_returns_false_for_missing_session()
{
    data::CashSessionRepository sessionRepo(*m_db);

    const int sessionId = sessionRepo.open(5000);
    QVERIFY(sessionId > 0);
    const int before = countCashSessions(*m_db);

    // An UPDATE matching nothing is still a successful statement, so this used to
    // return true for an id that was never there.
    QVERIFY(!sessionRepo.close(99999, 5000, 0, 0));

    // A refused close must leave every other row exactly as it was.
    QCOMPARE(countCashSessions(*m_db), before);
    const auto untouched = sessionRepo.findById(sessionId);
    QVERIFY(untouched.has_value());
    QCOMPARE(untouched->status, QStringLiteral("open"));
    QCOMPARE(untouched->closingCountedCents, 0LL);
    QCOMPARE(untouched->varianceCents, 0LL);
    QVERIFY(!untouched->closedAt.isValid());
    QVERIFY(sessionRepo.findOpen().has_value());

    // The real one still closes normally.
    QVERIFY(sessionRepo.close(sessionId, 5000, 5000, 0));
    QVERIFY(!sessionRepo.findOpen().has_value());
}

void DataLayerTest::open_refuses_when_session_already_open()
{
    data::CashSessionRepository sessionRepo(*m_db);

    // Earlier tests in this suite each leave a closed row behind, so the total
    // is only meaningful relative to where it started.
    const int rowsBefore = countCashSessions(*m_db);
    QCOMPARE(countOpenCashSessions(*m_db), 0);

    const int sessionId = sessionRepo.open(5000);
    QVERIFY(sessionId > 0);
    QCOMPARE(countOpenCashSessions(*m_db), 1);
    QCOMPARE(countCashSessions(*m_db), rowsBefore + 1);

    // A second open row would be invisible: findOpen() has no ORDER BY, so the
    // app would read one session at random and strand the other's movements.
    QCOMPARE(sessionRepo.open(3000), 0);
    QCOMPARE(countOpenCashSessions(*m_db), 1);
    QCOMPARE(countCashSessions(*m_db), rowsBefore + 1);

    // The survivor is the one that was opened first, untouched.
    const auto open = sessionRepo.findOpen();
    QVERIFY(open.has_value());
    QCOMPARE(open->id, sessionId);
    QCOMPARE(open->openingFloatCents, 5000LL);

    // And the day can now be closed, which is what frees the till again.
    QVERIFY(sessionRepo.close(sessionId, 5000, 5000, 0));
    QCOMPARE(countOpenCashSessions(*m_db), 0);

    // With no session open, opening is allowed again.
    const int next = sessionRepo.open(3000);
    QVERIFY(next > 0);
    QVERIFY(sessionRepo.close(next, 3000, 3000, 0));
}

// Captures warnings while a database is opened, so the degraded path in
// createSingleOpenSessionIndex() can be asserted rather than merely read.
QStringList g_capturedWarnings;
QtMessageHandler g_previousHandler = nullptr;

void captureWarningHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtWarningMsg) {
        g_capturedWarnings << message;
    }
    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    }
}

// Builds a database shaped like one written by an older build: the same table,
// no single-open index, and two sessions still open — the state that makes the
// index impossible to create.
void createLegacyDbWithTwoOpenSessions(const QString& path)
{
    const QString connectionName = QStringLiteral("legacy_open_sessions");
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(path);
    QVERIFY(db.open());

    QSqlQuery create(db);
    QVERIFY(create.exec(QStringLiteral(
        "CREATE TABLE cash_sessions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "opened_at TEXT NOT NULL,"
        "opening_float_cents INTEGER NOT NULL,"
        "closed_at TEXT NULL,"
        "closing_counted_cents INTEGER NULL,"
        "expected_cents INTEGER NULL,"
        "variance_cents INTEGER NULL,"
        "status TEXT NOT NULL DEFAULT 'open')")));
    QVERIFY(create.exec(QStringLiteral(
        "INSERT INTO cash_sessions (opened_at, opening_float_cents, status) "
        "VALUES ('2026-01-01T08:00:00.000', 5000, 'open'), ('2026-01-01T16:00:00.000', 3000, 'open')")));

    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
}

void DataLayerTest::legacy_db_with_two_open_sessions_still_opens()
{
    const QString path = m_dir.filePath(QStringLiteral("legacy_two_open.sqlite"));
    QFile::remove(path);
    createLegacyDbWithTwoOpenSessions(path);

    g_capturedWarnings.clear();
    g_previousHandler = qInstallMessageHandler(captureWarningHandler);

    // The whole reason the index is not part of the schema statement list: this
    // database cannot satisfy the index, and the app still has to start.
    bool threw = false;
    int openCount = -1;
    try {
        data::Database db(path);
        data::CashSessionRepository sessionRepo(db);
        openCount = countOpenCashSessions(db);
    } catch (const std::exception&) {
        threw = true;
    }
    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;

    QVERIFY(!threw);
    QCOMPARE(openCount, 2);

    // And the operator is told, rather than left with an unprotected database.
    bool warned = false;
    for (const QString& warning : g_capturedWarnings) {
        if (warning.contains(QLatin1StringView("idx_cash_sessions_one_open"))) {
            warned = true;
        }
    }
    QVERIFY(warned);

    // The index really is absent, which is the degraded state the warning names.
    // Reopening the file applies the schema again, so with the duplicate still in
    // place it must still fail, and must still not be fatal.
    {
        data::Database db(path);
        QVERIFY(!cashSessionIndexExists(db, QStringLiteral("idx_cash_sessions_one_open")));
    }

    // Recovery: close the extra session, and the next start restores the index,
    // which is what the warning tells the operator to do.
    {
        data::Database db(path);
        data::CashSessionRepository sessionRepo(db);
        QSqlQuery ids(db.handle());
        QVERIFY(ids.exec(QStringLiteral("SELECT id FROM cash_sessions WHERE status = 'open'")));
        QVERIFY(ids.next());
        const int staleId = ids.value(0).toInt();
        QVERIFY(sessionRepo.close(staleId, 3000, 3000, 0));
        QCOMPARE(countOpenCashSessions(db), 1);
    }
    {
        data::Database db(path);
        QVERIFY(cashSessionIndexExists(db, QStringLiteral("idx_cash_sessions_one_open")));
    }
}

void DataLayerTest::close_is_idempotent()
{
    data::CashSessionRepository sessionRepo(*m_db);

    const int sessionId = sessionRepo.open(5000);
    QVERIFY(sessionId > 0);

    QVERIFY(sessionRepo.close(sessionId, 5000, 5000, 0));
    const auto firstClose = sessionRepo.findById(sessionId);
    QVERIFY(firstClose.has_value());
    QCOMPARE(firstClose->status, QStringLiteral("closed"));
    const QDateTime firstClosedAt = firstClose->closedAt;
    QVERIFY(firstClosedAt.isValid());

    // A day that is already settled must not be settled twice. SQLite counts a
    // row as affected even when the new values equal the old ones, so this only
    // reports failure because the UPDATE asks for status = 'open'.
    QVERIFY(!sessionRepo.close(sessionId, 5000, 5000, 0));
    QVERIFY(!sessionRepo.close(sessionId, 999, 5000, -499));

    // And the rejected attempts changed nothing, including the timestamp.
    const auto after = sessionRepo.findById(sessionId);
    QVERIFY(after.has_value());
    QCOMPARE(after->status, QStringLiteral("closed"));
    QCOMPARE(after->closingCountedCents, firstClose->closingCountedCents);
    QCOMPARE(after->varianceCents, firstClose->varianceCents);
    QCOMPARE(after->closedAt, firstClosedAt);
}

void DataLayerTest::schema_has_single_open_index()
{
    bool isUnique = false;
    bool isPartial = false;
    QVERIFY(cashSessionIndexExists(*m_db, QStringLiteral("idx_cash_sessions_one_open"), &isUnique, &isPartial));

    // Partial and unique, not a plain unique index: a plain unique index on
    // status would cap the whole table at one row per status value, including
    // every closed session ever recorded.
    QVERIFY(isUnique);
    QVERIFY(isPartial);

    // Opening and closing repeatedly must keep working under the index, which
    // they would not if the index were global rather than partial.
    data::CashSessionRepository sessionRepo(*m_db);
    for (int i = 0; i < 3; ++i) {
        const int sessionId = sessionRepo.open(5000 + i);
        QVERIFY(sessionId > 0);
        QVERIFY(sessionRepo.close(sessionId, 5000, 5000, 0));
    }
    QCOMPARE(countOpenCashSessions(*m_db), 0);
}

void DataLayerTest::open_twice_via_repo_fails()
{
    data::CashSessionRepository sessionRepo(*m_db);
    QCOMPARE(countOpenCashSessions(*m_db), 0);
    const int rowsBefore = countCashSessions(*m_db);

    QVERIFY(sessionRepo.open(5000) > 0);
    QCOMPARE(sessionRepo.open(3000), 0);
    QCOMPARE(countOpenCashSessions(*m_db), 1);
    QCOMPARE(countCashSessions(*m_db), rowsBefore + 1);

    // The repository check is not the only line of defence. Bypassing it with raw
    // SQL has to fail too, otherwise any other code path that inserts a session
    // would quietly reintroduce the ambiguity findOpen() cannot resolve.
    QSqlQuery insert(m_db->handle());
    insert.prepare(QStringLiteral(
        "INSERT INTO cash_sessions (opened_at, opening_float_cents, status) VALUES (?, ?, 'open')"));
    insert.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    insert.addBindValue(3000);
    QVERIFY(!insert.exec());
    QCOMPARE(countOpenCashSessions(*m_db), 1);
    QCOMPARE(countCashSessions(*m_db), rowsBefore + 1);

    // Once the session is closed the slot is free again, for the repository and
    // for raw SQL alike.
    const auto open = sessionRepo.findOpen();
    QVERIFY(open.has_value());
    QVERIFY(sessionRepo.close(open->id, 5000, 5000, 0));
    QCOMPARE(countOpenCashSessions(*m_db), 0);
    QVERIFY(insert.exec());
    QCOMPARE(countOpenCashSessions(*m_db), 1);
    QCOMPARE(countCashSessions(*m_db), rowsBefore + 2);

    // Hand the suite back a closed till, the way it was found.
    const int rawSessionId = insert.lastInsertId().toInt();
    QVERIFY(rawSessionId > 0);
    QVERIFY(sessionRepo.close(rawSessionId, 5000, 5000, 0));
    QCOMPARE(countOpenCashSessions(*m_db), 0);
}

void DataLayerTest::device_ledger_rejects_empty_device_id()
{
    // A database of its own: this test opens a cash session, and the suite
    // shares one database whose open session every later test depends on.
    const QString path = m_dir.filePath(QStringLiteral("ledgeremptyid.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000099");
    product.name = QStringLiteral("منتج");
    product.costPriceCents = 9500;
    product.salePriceCents = 12000;
    product.unit = QStringLiteral("وحدة");
    product.packageSize = 1;
    const int productId = products.save(product);
    QVERIFY(productId > 0);

    core::StockMovement inbound;
    inbound.productId = productId;
    inbound.delta = 100;
    inbound.reason = QStringLiteral("initial stock");
    inbound.createdAt = QDateTime::currentDateTimeUtc();
    QVERIFY(data::StockMovementRepository(db).insert(inbound) > 0);

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 1.0;

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("عميل");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);

    // Two flavours of "no identity", and the difference matters. DeviceIdentity
    // returns QString() on failure, and Qt binds a null QString as SQL NULL, so
    // the sales table's NOT NULL constraint already rejects that one with a
    // driver error. A non-null but empty string binds as '' and would sail
    // straight into the row, so the service has to refuse it itself.
    const QString flavors[] = {QString(), QStringLiteral("")};
    for (const QString& flavor : flavors) {
        data::DeviceLedgerService ledger(db, flavor);
        QVERIFY2(!ledger.isValid(), qPrintable(flavor));

        // Every write goes through the same choke point, so all three must be
        // refused, and each with a message an operator can act on rather than
        // the empty string a swallowed driver error leaves behind.
        const data::DeviceOpResult sale = ledger.recordSale({item}, sessionId);
        QVERIFY2(!sale.ok, qPrintable(flavor));
        QVERIFY2(sale.error.contains(QLatin1StringView("device id is empty")), qPrintable(sale.error));

        const data::DeviceOpResult debt = ledger.recordCustomerDebt(customerId, {item});
        QVERIFY2(!debt.ok, qPrintable(flavor));
        QVERIFY2(debt.error.contains(QLatin1StringView("device id is empty")), qPrintable(debt.error));

        const data::DeviceOpResult payment = ledger.recordCustomerPayment(customerId, 1000, sessionId, QString());
        QVERIFY2(!payment.ok, qPrintable(flavor));
        QVERIFY2(payment.error.contains(QLatin1StringView("device id is empty")), qPrintable(payment.error));

        // Nothing was written: no sale row, no customer transaction, no payment,
        // and above all no outbox row the server could never attribute to a
        // device.
        QCOMPARE(data::SaleRepository(db).findBetween(g_reversalWindowFrom, g_reversalWindowTo).size(), 0);
        QCOMPARE(data::CustomerTransactionRepository(db)
                     .findBetween(g_reversalWindowFrom, g_reversalWindowTo)
                     .size(),
                 0);
        QCOMPARE(data::PaymentRepository(db).findBetween(g_reversalWindowFrom, g_reversalWindowTo).size(), 0);
        QCOMPARE(data::SyncOutboxRepository(db).countPending(), 0);

        // The op sequence must not have advanced either. A refused operation that
        // still consumed an op id would leave a permanent gap in the device's
        // sequence, which is documented as monotonic and never reused.
        QSqlQuery sequence(db.handle());
        QVERIFY(sequence.exec(QStringLiteral("SELECT value FROM sync_sequence WHERE id = 1")));
        QVERIFY(sequence.next());
        QCOMPARE(sequence.value(0).toLongLong(), 0LL);
    }

    // A service with a real id is unaffected, so the guard is not a blanket ban.
    data::DeviceLedgerService working(db, QStringLiteral("pos-device-1"));
    QVERIFY(working.isValid());
    const data::DeviceOpResult ok = working.recordSale({item}, sessionId);
    QVERIFY2(ok.ok, qPrintable(ok.error));
    QCOMPARE(data::SaleRepository(db).findBetween(g_reversalWindowFrom, g_reversalWindowTo).size(), 1);
    QVERIFY(data::SyncOutboxRepository(db).countPending() > 0);
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

void DataLayerTest::journalModeMatchesDatabaseRole()
{
    // WAL belongs to the central server so concurrent phone syncs never block;
    // the device (the default) keeps the plain journal.
    const QString serverPath = m_dir.filePath(QStringLiteral("server-role.sqlite"));
    const QString devicePath = m_dir.filePath(QStringLiteral("device-role.sqlite"));

    {
        data::Database serverDb(serverPath, data::DatabaseMode::Server);
        QSqlQuery query(serverDb.handle());
        QVERIFY(query.exec(QStringLiteral("PRAGMA journal_mode")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString().toLower(), QStringLiteral("wal"));
    }
    {
        data::Database deviceDb(devicePath);
        QSqlQuery query(deviceDb.handle());
        QVERIFY(query.exec(QStringLiteral("PRAGMA journal_mode")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString().toLower(), QStringLiteral("delete"));
    }
}

void DataLayerTest::pruneOnlyExpiredAcknowledgedOutboxRows()
{
    const QString path = m_dir.filePath(QStringLiteral("outbox-prune.sqlite"));
    data::Database db(path);
    data::SyncOutboxRepository outbox(db);

    const auto makeOp = [](int opId) {
        core::SyncOperation op;
        op.opId = opId;
        op.type = core::SyncOpType::Sale;
        op.amountCents = 0;
        op.occurredAt = QDateTime::currentDateTime();
        op.deviceId = QStringLiteral("dev-prune");
        return op;
    };

    const int ackedOld = outbox.enqueue(makeOp(1));
    const int ackedFresh = outbox.enqueue(makeOp(2));
    const int refused = outbox.enqueue(makeOp(3));
    const int stillPending = outbox.enqueue(makeOp(4));
    QVERIFY(ackedOld > 0 && ackedFresh > 0 && refused > 0 && stillPending > 0);
    QVERIFY(outbox.markApplied(ackedOld));
    QVERIFY(outbox.markApplied(ackedFresh));
    QVERIFY(outbox.markPermanentFailed(refused, QStringLiteral("server refused")));

    const QDateTime now = QDateTime::currentDateTime();
    const auto backdate = [&](int id, const QDateTime& when) {
        QSqlQuery update(db.handle());
        update.prepare(QStringLiteral("UPDATE sync_outbox SET created_at = ? WHERE id = ?"));
        update.addBindValue(data::toIso(when));
        update.addBindValue(id);
        QVERIFY(update.exec());
    };
    backdate(ackedOld, now.addDays(-40));
    backdate(refused, now.addDays(-40));
    backdate(stillPending, now.addDays(-40));

    QCOMPARE(outbox.pruneAppliedOlderThan(now.addDays(-30)), 1);

    QVERIFY(!outbox.findById(ackedOld).has_value());
    QVERIFY(outbox.findById(ackedFresh).has_value());    // fresh, kept
    QVERIFY(outbox.findById(refused).has_value());       // anomaly, kept for the owner
    const auto pending = outbox.findById(stillPending);
    QVERIFY(pending.has_value());
    QCOMPARE(pending->status, core::SyncOutboxStatus::Pending);
}

void DataLayerTest::appliedOpsJournalPrunesBoundedly()
{
    const QString path = m_dir.filePath(QStringLiteral("applied-prune.sqlite"));
    data::Database db(path, data::DatabaseMode::Server);
    data::AppliedOpRepository journal(db);

    const QDateTime old = QDateTime::currentDateTime().addDays(-500);
    const auto insertOld = [&](int opId) {
        core::AppliedOpRecord r;
        r.deviceId = QStringLiteral("dev-A");
        r.opId = opId;
        r.opType = static_cast<int>(core::SyncOpType::Sale);
        r.appliedAt = old;
        r.totalCents = 1000;
        QVERIFY(journal.insert(r) > 0);
    };
    for (int i = 0; i < 5; ++i) {
        insertOld(i + 1);
    }

    // Cleanup runs in bounded batches so a live sync never waits on it.
    QCOMPARE(journal.pruneOlderThan(QDateTime::currentDateTime(), 2), 2);
    QCOMPARE(journal.count(), 3);
    QCOMPARE(journal.pruneOlderThan(QDateTime::currentDateTime(), 2), 2);
    QCOMPARE(journal.count(), 1);

    core::AppliedOpRecord fresh;
    fresh.deviceId = QStringLiteral("dev-B");
    fresh.opId = 9;
    fresh.opType = static_cast<int>(core::SyncOpType::CustomerDebt);
    // Timestamped slightly in the future (clock skew happens in the field), so
    // it is guaranteed newer than any "now" cutoff in this test.
    fresh.appliedAt = QDateTime::currentDateTime().addSecs(3600);
    fresh.totalCents = 5000;
    QVERIFY(journal.insert(fresh) > 0);
    QCOMPARE(journal.count(), 2);
    QCOMPARE(journal.pruneOlderThan(QDateTime::currentDateTime(), 10), 1);
    QCOMPARE(journal.count(), 1);
}

void DataLayerTest::user_pin_roundtrip()
{
    const QString path = m_dir.filePath(QStringLiteral("user-pin.sqlite"));
    data::Database db(path);
    data::UserRepository repo(db);

    QVERIFY(!repo.hasAny());

    core::User user;
    user.name = QStringLiteral("أمين");
    user.role = QStringLiteral("manager");
    const int id = repo.save(user);
    QVERIFY(id > 0);
    QVERIFY(repo.hasAny());

    QVERIFY(repo.savePin(id, QStringLiteral("12")));

    const auto found = repo.findByPin(QStringLiteral("12"));
    QVERIFY(found.has_value());
    QCOMPARE(found->id, id);
    QCOMPARE(found->name, QStringLiteral("أمين"));
    QCOMPARE(found->active, true);

    QVERIFY(repo.setActive(id, false));
    QVERIFY(!repo.findByPin(QStringLiteral("12")).has_value());
    QVERIFY(repo.listActive().isEmpty());

    QVERIFY(repo.setActive(id, true));
    QVERIFY(repo.findByPin(QStringLiteral("12")).has_value());
    QCOMPARE(repo.listActive().size(), 1);

    QVERIFY(repo.savePin(id, QStringLiteral("34")));
    QVERIFY(!repo.findByPin(QStringLiteral("12")).has_value());
    const auto rotated = repo.findByPin(QStringLiteral("34"));
    QVERIFY(rotated.has_value());
    QCOMPARE(rotated->id, id);

    const auto byName = repo.findByName(QStringLiteral("أمين"));
    QVERIFY(byName.has_value());
    QCOMPARE(byName->id, id);
    QVERIFY(!repo.findByName(QStringLiteral("مجهول")).has_value());
}

void DataLayerTest::admin_master_roundtrip()
{
    const QString path = m_dir.filePath(QStringLiteral("admin-master.sqlite"));
    QFile::remove(path);
    data::Database db(path);
    data::UserRepository userRepo(db);
    data::AdminSecretRepository secretRepo(db);

    core::User admin;
    admin.name = QStringLiteral("المدير");
    admin.role = QStringLiteral("admin");
    const int id = userRepo.save(admin);
    QVERIFY(id > 0);
    QVERIFY(userRepo.savePin(id, QStringLiteral("12")));

    QVERIFY(secretRepo.setMaster(id, QStringLiteral("secret")));
    QVERIFY(secretRepo.verifyMaster(id, QStringLiteral("secret")));
    QVERIFY(!secretRepo.verifyMaster(id, QStringLiteral("wrong")));

    const auto found = secretRepo.findAdminByMaster(QStringLiteral("secret"));
    QVERIFY(found.has_value());
    QCOMPARE(*found, id);

    QVERIFY(!secretRepo.findAdminByMaster(QStringLiteral("wrong")).has_value());
}

void DataLayerTest::language_setting_persists()
{
    data::SettingRepository settings(*m_db);

    // First launch has no value yet, so i18n reports the Arabic default.
    QCOMPARE(core::currentLanguage(*m_db), core::defaultLanguage());

    settings.set(QStringLiteral("language"), QStringLiteral("fr"));
    QCOMPARE(settings.value(QStringLiteral("language")),
             std::optional<QString>(QStringLiteral("fr")));
    QCOMPARE(core::currentLanguage(*m_db), QStringLiteral("fr"));

    // Overwriting the same key must replace, not duplicate.
    settings.set(QStringLiteral("language"), QStringLiteral("en"));
    QCOMPARE(settings.value(QStringLiteral("language")),
             std::optional<QString>(QStringLiteral("en")));
    QCOMPARE(core::currentLanguage(*m_db), QStringLiteral("en"));

    // An unsupported value must not win over the default.
    settings.set(QStringLiteral("language"), QStringLiteral("de"));
    QCOMPARE(core::currentLanguage(*m_db), core::defaultLanguage());
}

void DataLayerTest::repository_error_is_recorded()
{
    data::ProductRepository products(*m_db);

    // A write that has to succeed, so the failure below cannot pass for the
    // wrong reason by being caused by a broken database.
    core::Product good;
    good.barcode = QStringLiteral("REC-0001");
    good.name = QStringLiteral("Recorder Widget");
    good.costPriceCents = 100;
    good.salePriceCents = 200;
    const int savedId = products.save(good);
    QVERIFY2(savedId > 0,
             qPrintable(QStringLiteral("valid insert failed: %1").arg(m_db->lastError())));

    // Second row with the same barcode: the UNIQUE index rejects it and save()
    // still returns 0. The return value is not the thing under test here, it is
    // the guarantee that it does not move. The reason is: before repositories
    // reported to Database, the driver error was dropped on the floor and the
    // service showed the operator a blank message for a write that was refused.
    core::Product duplicate;
    duplicate.barcode = good.barcode;
    duplicate.name = QStringLiteral("Recorder Widget Duplicate");
    duplicate.costPriceCents = 1;
    duplicate.salePriceCents = 2;
    QCOMPARE(products.save(duplicate), 0);

    const QString reason = m_db->lastError();
    QVERIFY2(!reason.isEmpty(), "a refused write must leave a reason behind");
    QCOMPARE(m_db->lastErrorContext(), QStringLiteral("ProductRepository::save"));
    QVERIFY2(reason.contains(m_db->lastErrorContext()),
             qPrintable(QStringLiteral("context missing from message: %1").arg(reason)));
    QVERIFY2(reason.contains(QStringLiteral("UNIQUE"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("driver text missing from message: %1").arg(reason)));

    // The rejected row must not have been written, and the one that did succeed
    // must still be there: recording the error is not allowed to half-apply.
    const auto found = products.findByBarcode(good.barcode);
    QVERIFY(found.has_value());
    QCOMPARE(found->id, savedId);
    QCOMPARE(found->name, QStringLiteral("Recorder Widget"));

    // Leave the shared suite database as we found it.
    QSqlQuery cleanup(m_db->handle());
    QVERIFY(cleanup.exec(QStringLiteral("DELETE FROM products WHERE barcode = 'REC-0001'")));
}

void DataLayerTest::last_error_clears_on_success()
{
    // A database of its own: the point is to observe one failure followed by one
    // success, and the suite-wide m_db carries state from every other test.
    const QString path = m_dir.filePath(QStringLiteral("clear_error.sqlite"));
    data::Database db(path);
    data::ProductRepository products(db);

    // Seed one row so the failing write has something to collide with.
    core::Product seed;
    seed.barcode = QStringLiteral("CLR-0001");
    seed.name = QStringLiteral("Clear Seed");
    QVERIFY(products.save(seed) > 0);

    // Refused write: the reason has to be there, otherwise the test proves
    // nothing about clearing it.
    core::Product duplicate;
    duplicate.barcode = seed.barcode;
    duplicate.name = QStringLiteral("Clear Duplicate");
    QCOMPARE(products.save(duplicate), 0);
    QVERIFY2(!db.lastError().isEmpty(), "the refused write must leave a reason behind");
    QCOMPARE(db.lastErrorContext(), QStringLiteral("ProductRepository::save"));

    // Accepted write. Nothing failed this time, so a leftover reason would be a
    // lie: a caller that reads lastError() after this point would report the
    // earlier rejection against a row that is now in the table.
    core::Product fresh;
    fresh.barcode = QStringLiteral("CLR-0002");
    fresh.name = QStringLiteral("Clear Fresh");
    const int freshId = products.save(fresh);
    QVERIFY2(freshId > 0, qPrintable(QStringLiteral("fresh insert failed: %1").arg(db.lastError())));
    QVERIFY2(db.lastError().isEmpty(),
             qPrintable(QStringLiteral("stale reason survived a good write: %1").arg(db.lastError())));
    QVERIFY(db.lastErrorContext().isEmpty());

    // The claim is that the row really is there, not merely that the error slot
    // was emptied to hide a failure.
    const auto found = products.findByBarcode(fresh.barcode);
    QVERIFY(found.has_value());
    QCOMPARE(found->id, freshId);
    QCOMPARE(found->name, QStringLiteral("Clear Fresh"));
    QVERIFY2(db.lastError().isEmpty(), "reading it back must not resurrect a reason");
}

void DataLayerTest::reverseSale_returns_error_on_missing_sale()
{
    // A sale id that cannot exist. The old signature could only answer 0 here,
    // so the caller had nothing but a canned "تعذر استرداد المبيع" to show; the
    // point of the struct is that the refusal now carries a reason.
    data::SaleService service(*m_db);
    const data::SaleReverseResult result = service.reverseSale(99999, 0);
    QVERIFY(!result.ok);
    QVERIFY2(!result.error.isEmpty(), "a refused reversal must say why");

    // Nothing may be written on the way out, and the shared database must not
    // have been left holding a transaction open by the early return.
    QSqlQuery check(m_db->handle());
    QVERIFY(check.exec(QStringLiteral("SELECT COUNT(*) FROM sales WHERE reversed_sale_id = 99999")));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toInt(), 0);
}

void DataLayerTest::payment_reverse_returns_false_on_missing()
{
    // reverse() was void, so this refusal was invisible: the service went on to
    // write the refund cash movement and commit, leaving money in the till for a
    // reversal that never happened.
    data::PaymentRepository payments(*m_db);
    QVERIFY(!payments.reverse(99999, 5000, QStringLiteral("refund of a sale that never was")));

    QSqlQuery check(m_db->handle());
    QVERIFY(check.exec(QStringLiteral("SELECT COUNT(*) FROM payments WHERE reversed_id = 99999")));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toInt(), 0);
}

void DataLayerTest::reverseSale_refuses_double_reversal()
{
    // Its own database: this writes a sale, a session and cash movements, and
    // the suite-wide m_db is shared with tests that count rows in a window.
    data::Database db(m_dir.filePath(QStringLiteral("double_reversal.sqlite")));
    data::SaleRepository sales(db);
    data::CashSessionRepository sessions(db);
    data::SaleService service(db);

    const int sessionId = sessions.open(100000);
    QVERIFY(sessionId > 0);

    core::Sale sale;
    sale.createdAt = QDateTime::currentDateTime();
    sale.totalCents = 5000;
    sale.deviceId = QStringLiteral("dev-double-reversal");
    const int saleId = sales.insert(sale);
    QVERIFY(saleId > 0);

    const data::SaleReverseResult first = service.reverseSale(saleId, sessionId);
    QVERIFY2(first.ok, qPrintable(QStringLiteral("first reversal failed: %1").arg(first.error)));

    // The second click. reversed_sale_id carries no unique index, so without the
    // lookup the till gets the money back twice and two mirrored sales sit on
    // the ledger.
    const data::SaleReverseResult second = service.reverseSale(saleId, sessionId);
    QVERIFY(!second.ok);
    QCOMPARE(second.error, QStringLiteral("reverseSale: already reversed"));

    QSqlQuery check(db.handle());
    QVERIFY(check.exec(QStringLiteral("SELECT COUNT(*) FROM sales WHERE reversed_sale_id = %1").arg(saleId)));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toInt(), 1);

    // The refusal has to hand the connection back with no transaction still
    // open. A read would not notice, because a leftover transaction still
    // answers queries; the next reversal is what breaks, since it cannot begin
    // a second one and would report "could not start the transaction" for the
    // rest of the session. Reversing a different sale is what pins it.
    core::Sale other;
    other.createdAt = QDateTime::currentDateTime();
    other.totalCents = 2500;
    other.deviceId = QStringLiteral("dev-double-reversal");
    const int otherId = sales.insert(other);
    QVERIFY(otherId > 0);

    const data::SaleReverseResult third = service.reverseSale(otherId, sessionId);
    QVERIFY2(third.ok,
             qPrintable(QStringLiteral("a refused reversal left a transaction open: %1")
                            .arg(third.error)));
}

void DataLayerTest::overflow_guard()
{
    QVector<core::SaleItem> ordinary;
    core::SaleItem line;
    line.productId = 1;
    line.unitPriceCents = 100;
    line.quantity = 5;
    ordinary.append(line);
    QCOMPARE(data::totalCentsFor(ordinary), std::optional<long long>(500));

    // A sold-by-weight line is entered in kilograms, so the operator can type a
    // quantity large enough to run the multiplication off the end of the range.
    // Signed overflow is undefined behaviour, so the multiplication below is the
    // thing being tested: it must refuse rather than wrap into a total that
    // looks like money.
    QVector<core::SaleItem> overflowing;
    line.unitPriceCents = std::numeric_limits<long long>::max();
    line.quantity = 2;
    overflowing.append(line);
    QVERIFY2(!data::totalCentsFor(overflowing).has_value(),
             "a total past the range must be refused, not wrapped");

    // Two lines that each fit but do not fit together: the running sum is a
    // second way out, and guarding only the multiplication would miss it.
    QVector<core::SaleItem> summing;
    line.unitPriceCents = std::numeric_limits<long long>::max() / 2 + 1;
    line.quantity = 1;
    summing.append(line);
    summing.append(line);
    QVERIFY2(!data::totalCentsFor(summing).has_value(),
             "lines that overflow only once added must be refused too");

    // And the guard must not fire on ordinary arithmetic, including the largest
    // total that is genuinely representable.
    QVector<core::SaleItem> boundary;
    line.unitPriceCents = std::numeric_limits<long long>::max();
    line.quantity = 1;
    boundary.append(line);
    QCOMPARE(data::totalCentsFor(boundary), std::optional<long long>(std::numeric_limits<long long>::max()));
    QVERIFY(data::totalCentsFor({}).has_value());
    QCOMPARE(data::totalCentsFor({}), std::optional<long long>(0));
}

void DataLayerTest::overflow_guard_cogs()
{
    QVector<core::SaleItem> ordinary;
    core::SaleItem line;
    line.productId = 1;
    line.unitCostCents = 100;
    line.quantity = 5;
    ordinary.append(line);
    QCOMPARE(data::cogsCentsFor(ordinary), std::optional<long long>(500));

    // Cost is multiplied by quantity the same way price is, and it reaches the
    // profit and loss statement, so it gets the same guard.
    QVector<core::SaleItem> overflowing;
    line.unitCostCents = std::numeric_limits<long long>::max();
    line.quantity = 2;
    overflowing.append(line);
    QVERIFY2(!data::cogsCentsFor(overflowing).has_value(),
             "a cost past the range must be refused, not wrapped");

    QVector<core::SaleItem> summing;
    line.unitCostCents = std::numeric_limits<long long>::max() / 2 + 1;
    line.quantity = 1;
    summing.append(line);
    summing.append(line);
    QVERIFY2(!data::cogsCentsFor(summing).has_value(),
             "lines that overflow only once added must be refused too");

    QVector<core::SaleItem> boundary;
    line.unitCostCents = std::numeric_limits<long long>::max();
    line.quantity = 1;
    boundary.append(line);
    QCOMPARE(data::cogsCentsFor(boundary),
             std::optional<long long>(std::numeric_limits<long long>::max()));
    QCOMPARE(data::cogsCentsFor({}), std::optional<long long>(0));
}

void DataLayerTest::supplier_new_fields_roundtrip()
{
    data::SupplierRepository suppliers(*m_db);

    core::Supplier s;
    s.name = QStringLiteral("Test Supplier");
    s.phone = QStringLiteral("0123");
    s.address = QStringLiteral("Rue X");
    s.notes = QStringLiteral("test");
    s.openingBalanceCents = 50000;
    s.active = true;

    const int id = suppliers.save(s);
    QVERIFY(id > 0);

    const auto found = suppliers.findById(id);
    QVERIFY(found.has_value());
    QCOMPARE(found->name, QStringLiteral("Test Supplier"));
    QCOMPARE(found->phone, QStringLiteral("0123"));
    QCOMPARE(found->address, QStringLiteral("Rue X"));
    QCOMPARE(found->notes, QStringLiteral("test"));
    QCOMPARE(found->openingBalanceCents, 50000LL);
    QVERIFY(found->active);
}

void DataLayerTest::supplier_list_active_excludes_inactive()
{
    // Dedicated DB: the suite-wide m_db already has active suppliers from
    // other tests, so listActive() would return more than just our two.
    data::Database db(m_dir.filePath(QStringLiteral("supplier_active.sqlite")));
    data::SupplierRepository suppliers(db);

    core::Supplier activeSupplier;
    activeSupplier.name = QStringLiteral("Active One");
    activeSupplier.active = true;
    QVERIFY(suppliers.save(activeSupplier) > 0);

    core::Supplier inactiveSupplier;
    inactiveSupplier.name = QStringLiteral("Inactive One");
    inactiveSupplier.active = false;
    QVERIFY(suppliers.save(inactiveSupplier) > 0);

    const QVector<core::Supplier> active = suppliers.listActive();
    QCOMPARE(active.size(), 1);
    QCOMPARE(active[0].name, QStringLiteral("Active One"));
}

namespace {

// A purchase header, so a payment has a real invoice to point at. The supplier
// and the invoice are part of the fixture because every check below is about
// what a payment remembers, not about creating either of them.
int addPurchase(data::PurchaseRepository& purchases, int supplierId, long long totalCents)
{
    core::Purchase purchase;
    purchase.supplierId = supplierId;
    // purchases.invoice_number and purchases.note are NOT NULL, and the struct
    // leaves both as a null QString, which the driver binds as NULL.
    purchase.invoiceNumber = QStringLiteral("");
    purchase.note = QStringLiteral("");
    purchase.purchasedAt = data::nowIso();
    purchase.totalCents = totalCents;
    purchase.subtotalCents = totalCents;
    purchase.createdAt = data::nowIso();
    return purchases.insert(purchase);
}

int addSupplierForPayments(data::SupplierRepository& suppliers, const QString& name)
{
    core::Supplier supplier;
    supplier.name = name;
    return suppliers.save(supplier);
}

} // namespace

void DataLayerTest::supplier_balance_uses_opening_and_transactions()
{
    data::SupplierRepository suppliers(*m_db);
    data::PurchaseRepository purchases(*m_db);
    data::SupplierPaymentRepository payments(*m_db);

    core::Supplier s;
    s.name = QStringLiteral("Balance Test");
    s.openingBalanceCents = 10000;
    s.active = true;
    const int id = suppliers.save(s);
    QVERIFY(id > 0);

    // 10000 opening, 50000 invoiced, 20000 paid: the supplier is still owed
    // 40000. Both the invoice and the payment are checked on their own so a
    // balance that happens to come out right cannot hide a row that was missed.
    const int purchaseId = addPurchase(purchases, id, 50000);
    QVERIFY(purchaseId > 0);
    QCOMPARE(suppliers.balanceCentsFor(id), 60000LL);

    core::SupplierPayment payment;
    payment.supplierId = id;
    payment.purchaseId = purchaseId;
    payment.amountCents = 20000;
    payment.paidAt = data::nowIso();
    payment.createdAt = data::nowIso();
    QVERIFY(payments.insert(payment) > 0);

    QCOMPARE(suppliers.balanceCentsFor(id), 40000LL);

    // A second supplier shares the tables but not the ledger.
    core::Supplier other;
    other.name = QStringLiteral("Other Balance Test");
    const int otherId = suppliers.save(other);
    QVERIFY(otherId > 0);
    QCOMPARE(suppliers.balanceCentsFor(otherId), 0LL);
}

void DataLayerTest::supplier_payment_roundtrip()
{
    data::SupplierRepository suppliers(*m_db);
    data::PurchaseRepository purchases(*m_db);
    data::SupplierPaymentRepository payments(*m_db);

    const int supplierId = addSupplierForPayments(suppliers, QStringLiteral("Payment Supplier"));
    QVERIFY(supplierId > 0);
    const int purchaseId = addPurchase(purchases, supplierId, 100000);
    QVERIFY(purchaseId > 0);

    core::SupplierPayment payment;
    payment.supplierId = supplierId;
    payment.purchaseId = purchaseId;
    payment.amountCents = 30000;
    payment.paidAt = data::nowIso();
    payment.note = QStringLiteral("test");
    payment.createdAt = data::nowIso();
    const int paymentId = payments.insert(payment);
    QVERIFY(paymentId > 0);

    const auto stored = payments.findById(paymentId);
    QVERIFY(stored.has_value());
    QCOMPARE(stored->id, paymentId);
    QCOMPARE(stored->supplierId, supplierId);
    QVERIFY(stored->purchaseId.has_value());
    QCOMPARE(*stored->purchaseId, purchaseId);
    QCOMPARE(stored->amountCents, 30000LL);
    QCOMPARE(stored->paidAt, payment.paidAt);
    QCOMPARE(stored->note, QStringLiteral("test"));
    QCOMPARE(stored->createdAt, payment.createdAt);

    // A payment with no invoice behind it stays a general payment, so the
    // caller can tell "not tied to a purchase" from "the id is zero".
    core::SupplierPayment general;
    general.supplierId = supplierId;
    general.amountCents = 1000;
    general.paidAt = data::nowIso();
    general.createdAt = data::nowIso();
    const int generalId = payments.insert(general);
    QVERIFY(generalId > 0);
    const auto generalStored = payments.findById(generalId);
    QVERIFY(generalStored.has_value());
    QVERIFY(!generalStored->purchaseId.has_value());
}

void DataLayerTest::supplier_payment_find_by_supplier()
{
    data::SupplierRepository suppliers(*m_db);
    data::SupplierPaymentRepository payments(*m_db);

    const int first = addSupplierForPayments(suppliers, QStringLiteral("Payment Supplier A"));
    QVERIFY(first > 0);
    const int second = addSupplierForPayments(suppliers, QStringLiteral("Payment Supplier B"));
    QVERIFY(second > 0);

    // Two payments for the first supplier, one older than the other, so the
    // ordering is checked against known values rather than insertion order.
    const QString older = data::toIso(QDateTime::currentDateTime().addSecs(-600));
    const QString newer = data::toIso(QDateTime::currentDateTime().addSecs(-60));
    core::SupplierPayment oldPayment;
    oldPayment.supplierId = first;
    oldPayment.amountCents = 1000;
    oldPayment.paidAt = older;
    oldPayment.createdAt = data::nowIso();
    QVERIFY(payments.insert(oldPayment) > 0);
    core::SupplierPayment newPayment;
    newPayment.supplierId = first;
    newPayment.amountCents = 2000;
    newPayment.paidAt = newer;
    newPayment.createdAt = data::nowIso();
    QVERIFY(payments.insert(newPayment) > 0);
    core::SupplierPayment otherPayment;
    otherPayment.supplierId = second;
    otherPayment.amountCents = 3000;
    otherPayment.paidAt = newer;
    otherPayment.createdAt = data::nowIso();
    QVERIFY(payments.insert(otherPayment) > 0);

    const auto found = payments.findBySupplierId(first);
    QCOMPARE(found.size(), std::size_t(2));
    // Newest first: the payment recorded last is the one being asked about.
    QCOMPARE(found[0].amountCents, 2000LL);
    QCOMPARE(found[1].amountCents, 1000LL);
    for (const core::SupplierPayment& payment : found) {
        QCOMPARE(payment.supplierId, first);
    }

    QCOMPARE(payments.findBySupplierId(second).size(), std::size_t(1));
}

void DataLayerTest::supplier_payment_find_by_purchase()
{
    data::SupplierRepository suppliers(*m_db);
    data::PurchaseRepository purchases(*m_db);
    data::SupplierPaymentRepository payments(*m_db);

    const int supplierId = addSupplierForPayments(suppliers, QStringLiteral("Invoice Supplier"));
    QVERIFY(supplierId > 0);
    const int firstPurchase = addPurchase(purchases, supplierId, 100000);
    QVERIFY(firstPurchase > 0);
    const int secondPurchase = addPurchase(purchases, supplierId, 50000);
    QVERIFY(secondPurchase > 0);

    // One invoice paid in two instalments, another paid once.
    for (const long long amount : {40000LL, 60000LL}) {
        core::SupplierPayment part;
        part.supplierId = supplierId;
        part.purchaseId = firstPurchase;
        part.amountCents = amount;
        part.paidAt = data::nowIso();
        part.createdAt = data::nowIso();
        QVERIFY(payments.insert(part) > 0);
    }
    core::SupplierPayment full;
    full.supplierId = supplierId;
    full.purchaseId = secondPurchase;
    full.amountCents = 50000;
    full.paidAt = data::nowIso();
    full.createdAt = data::nowIso();
    QVERIFY(payments.insert(full) > 0);

    const auto found = payments.findByPurchaseId(firstPurchase);
    QCOMPARE(found.size(), std::size_t(2));
    long long total = 0;
    for (const core::SupplierPayment& payment : found) {
        QVERIFY(payment.purchaseId.has_value());
        QCOMPARE(*payment.purchaseId, firstPurchase);
        total += payment.amountCents;
    }
    QCOMPARE(total, 100000LL);
    QCOMPARE(payments.findByPurchaseId(secondPurchase).size(), std::size_t(1));
}

void DataLayerTest::supplier_payment_schema_exists()
{
    QSqlQuery query(m_db->handle());
    QVERIFY(query.exec(QStringLiteral(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name = 'supplier_payments'")));
    QVERIFY(query.next());

    // Both indexes exist: one to list a supplier's payments newest first, one to
    // sum what a single invoice has already been paid.
    QVERIFY(query.exec(QStringLiteral("PRAGMA index_list(supplier_payments)")));
    QStringList indexes;
    while (query.next()) {
        indexes << query.value(1).toString();
    }
    QVERIFY(indexes.contains(QStringLiteral("idx_supplier_payments_supplier")));
    QVERIFY(indexes.contains(QStringLiteral("idx_supplier_payments_purchase")));
    QCOMPARE(indexes.size(), 2);
}

QTEST_GUILESS_MAIN(DataLayerTest)
#include "tst_data.moc"