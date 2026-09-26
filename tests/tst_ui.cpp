#include <QtTest/QtTest>

#include <QDate>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTime>
#include <QTableWidget>
#include <QUrl>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QTimer>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include "ui/login_dialog.h"
#include "ui/users_page.h"

#include "core/session.h"
#include "core/sync_operation.h"
#include "data/admin_secret_repository.h"
#include "data/applied_op_repository.h"
#include "data/audit_log_repository.h"
#include "data/cash_entry_service.h"
#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/customer_transaction_repository.h"
#include "data/expense_repository.h"
#include "data/owner_drawing_repository.h"
#include "data/payment_repository.h"
#include "data/payment_service.h"
#include "data/product_repository.h"
#include "data/report_service.h"
#include "data/sale_item_repository.h"
#include "data/sale_repository.h"
#include "data/sale_service.h"
#include "data/setting_repository.h"
#include "data/stock_movement_repository.h"
#include "data/supplier_repository.h"
#include "data/supplier_transaction_repository.h"
#include "data/zakat_setting_repository.h"
#include "data/user_repository.h"
#include "network/sync_client.h"
#include "ui/audit_log_page.h"
#include "ui/cash_session_page.h"
#include "ui/customers_page.h"
#include "ui/expenses_page.h"
#include "ui/format_utils.h"
#include "ui/pos_page.h"
#include "ui/products_page.h"
#include "ui/refunds_page.h"
#include "ui/reports_page.h"
#include "ui/sales_page.h"
#include "ui/server_controller.h"
#include "ui/settings_page.h"
#include "ui/suppliers_page.h"
#include "ui/main_window.h"

#include <algorithm>

using namespace app;

// Phase 11+12 (desktop): the headless ServerController (sync hub + daily
// retention), the master-data pages, and the cash & sales flow: a fast POS
// page with flexible price/quantity entry, the cash session lifecycle, and
// today's sales summary.
class UiTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void serverStartStop();
    void serverAppliesOverLoopback();
    void serverEmitsStatsOnTimer();
    void serverRetentionPrunesOldOps();
    void pagesReflectSeededData();
    void flexibleAmountParsing();
    void posSaleWithPriceOverride();
    void posSaleRequiresOpenSession();
    void cashSessionLifecycle();
    void salesPageShowsToday();
    void profit_cents_survives_translation();
    void barcode_dialog_does_not_close();
    void customerCreditAndPayment();
    void expensesAndDrawings();
    void reportBuilds();
    void settingsCurrencyAndZakat();
    void refundsRestoreMoneyAndStock();
    void paymentRefundRaisesBalance();
    void entryReversal();
    void login_basic();
    // void statusBarSwitchUserButton();
    void users_page_lists_users();
    void users_page_shows_all_pins();
    void user_edit_pin_persists();
    void master_recovery_grants_access();

private:
    void seedSyncDatabase(const QString& path, int* productId, int* sessionId);
    void seedMasterData(const QString& path);
    int seedProduct(const QString& path, int* sessionId, long long salePriceCents);

    QTemporaryDir m_dir;
    QByteArray m_key;
};

void UiTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_key = QByteArrayLiteral("phase11-ui-key");
    app::ui::setCurrencySymbol(QString());
}

void UiTest::seedSyncDatabase(const QString& path, int* productId, int* sessionId)
{
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);

    data::CashSessionRepository sessions(db);
    *sessionId = sessions.open(5000);
    QVERIFY(*sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 7500;
    product.salePriceCents = 10000;
    product.unit = QStringLiteral("أنبوب");
    product.packageSize = 1;
    *productId = products.save(product);
    QVERIFY(*productId > 0);

    products.adjustStock(*productId, 100, QStringLiteral("purchase"));
}

void UiTest::seedMasterData(const QString& path)
{
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product a;
    a.barcode = QStringLiteral("1000000000001");
    a.name = QStringLiteral("أصناف أ");
    a.costPriceCents = 6000;
    a.salePriceCents = 10000;
    const int aId = products.save(a);
    QVERIFY(aId > 0);
    products.adjustStock(aId, 10, QStringLiteral("purchase"));

    core::Product b;
    b.barcode = QStringLiteral("1000000000002");
    b.name = QStringLiteral("أصناف ب");
    b.costPriceCents = 3000;
    b.salePriceCents = 5000;
    const int bId = products.save(b);
    QVERIFY(bId > 0);

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("زبون");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);

    core::CustomerTransaction debt;
    debt.customerId = customerId;
    debt.amountCents = 70000;
    QVERIFY(data::CustomerTransactionRepository(db).insert(debt) > 0);

    core::Payment payment;
    payment.customerId = customerId;
    payment.amountCents = 50000;
    QVERIFY(data::PaymentRepository(db).insert(payment) > 0);

    data::SupplierRepository suppliers(db);
    core::Supplier supplier;
    supplier.name = QStringLiteral("مورد الشاي");
    const int supplierId = suppliers.save(supplier);
    QVERIFY(supplierId > 0);

    core::SupplierTransaction invoice;
    invoice.supplierId = supplierId;
    invoice.amountCents = 250000;
    invoice.note = QStringLiteral("فاتورة");
    invoice.createdAt = QDateTime::currentDateTime();
    QVERIFY(data::SupplierTransactionRepository(db).insert(invoice) > 0);
}

void UiTest::serverStartStop()
{
    const QString path = m_dir.filePath(QStringLiteral("ctrl.sqlite"));
    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);

    QVERIFY(!controller.isListening());
    QVERIFY(controller.start());
    QVERIFY(controller.isListening());
    QVERIFY(controller.port() != 0);
    controller.stop();
    QVERIFY(!controller.isListening());
}

void UiTest::serverAppliesOverLoopback()
{
    int productId = 0;
    int sessionId = 0;
    const QString path = m_dir.filePath(QStringLiteral("loop.sqlite"));
    seedSyncDatabase(path, &productId, &sessionId);

    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);
    controller.setStatsIntervalMs(25);
    QVERIFY(controller.start());

    core::SyncOperation op;
    op.opId = 1;
    op.type = core::SyncOpType::Sale;
    op.occurredAt = QDateTime::currentDateTimeUtc();
    op.deviceId = QStringLiteral("ui-loop-device");
    core::SyncItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 0; // resolved from product.salePriceCents
    op.items = { item };

    QUrl endpoint;
    endpoint.setScheme(QStringLiteral("http"));
    endpoint.setHost(QStringLiteral("127.0.0.1"));
    endpoint.setPort(controller.port());
    endpoint.setPath(QStringLiteral("/api/sync"));

    const network::SyncSendResult result = network::SyncClient::sendBatch(endpoint, m_key, { op });
    QVERIFY(result.delivered);
    QCOMPARE(result.errorClass, network::SyncErrorClass::None);
    QCOMPARE(result.acks.size(), 1);
    QVERIFY(result.acks[0].ok);

    // Give the stats timer a beat to refresh, then verify live counters.
    QTest::qWait(150);
    const ui::ServerController::Stats stats = controller.stats();
    QCOMPARE(stats.appliedOps, 1);
    QCOMPARE(stats.devices, 1);
    QCOMPARE(stats.salesToday, 1);
    QCOMPARE(stats.revenueTodayCents, 20000LL);
    QVERIFY(stats.listening);

    data::SaleRepository sales(db);
    QCOMPARE(sales.countByDeviceId(QStringLiteral("ui-loop-device")), 1);
    controller.stop();
}

void UiTest::serverEmitsStatsOnTimer()
{
    const QString path = m_dir.filePath(QStringLiteral("timer.sqlite"));
    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);
    controller.setStatsIntervalMs(25);

    QSignalSpy spy(&controller, &ui::ServerController::statsChanged);
    QVERIFY(controller.start());
    QTest::qWait(200);
    QVERIFY(spy.count() >= 3);
    controller.stop();
}

void UiTest::serverRetentionPrunesOldOps()
{
    const QString path = m_dir.filePath(QStringLiteral("retention.sqlite"));
    data::Database db(path, data::DatabaseMode::Server);
    ui::ServerController controller(db, m_key);

    data::AppliedOpRepository journal(db);
    core::AppliedOpRecord old;
    old.deviceId = QStringLiteral("dev-old");
    old.opId = 1;
    old.opType = static_cast<int>(core::SyncOpType::Sale);
    old.appliedAt = QDateTime::currentDateTime().addDays(-500);
    old.totalCents = 1000;
    QVERIFY(journal.insert(old) > 0);

    core::AppliedOpRecord fresh;
    fresh.deviceId = QStringLiteral("dev-old");
    fresh.opId = 2;
    fresh.opType = static_cast<int>(core::SyncOpType::CustomerDebt);
    fresh.appliedAt = QDateTime::currentDateTime().addSecs(3600);
    fresh.totalCents = 5000;
    QVERIFY(journal.insert(fresh) > 0);

    QCOMPARE(controller.runRetention(30), 1);
    QCOMPARE(journal.count(), 1);
    QVERIFY(!journal.findByDeviceOp(QStringLiteral("dev-old"), 1).has_value());
    QVERIFY(journal.findByDeviceOp(QStringLiteral("dev-old"), 2).has_value());
}

void UiTest::pagesReflectSeededData()
{
    const QString path = m_dir.filePath(QStringLiteral("pages.sqlite"));
    seedMasterData(path);
    data::Database db(path);

    ui::ProductsPage products(db);
    QCOMPARE(products.rowCount(), 2);

    ui::CustomersPage customers(db);
    QCOMPARE(customers.rowCount(), 1);
    QCOMPARE(customers.balanceAt(0), ui::formatMoney(20000));

    ui::SuppliersPage suppliers(db);
    QCOMPARE(suppliers.supplierCount(), 1);
    suppliers.suppliersTable()->setCurrentCell(0, 0);
    QCOMPARE(suppliers.transactionCount(), 1);
}

int UiTest::seedProduct(const QString& path, int* sessionId, long long salePriceCents)
{
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    *sessionId = sessions.open(5000);
    if (*sessionId <= 0) {
        return 0;
    }

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 6000;
    product.salePriceCents = salePriceCents;
    product.unit = QStringLiteral("أنبوب");
    product.packageSize = 1;
    const int productId = products.save(product);
    if (productId <= 0) {
        return 0;
    }
    products.adjustStock(productId, 100, QStringLiteral("purchase"));
    return productId;
}

void UiTest::flexibleAmountParsing()
{
    QCOMPARE(ui::parseMoney(QStringLiteral("12")).value_or(-1), 1200LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("12.5")).value_or(-1), 1250LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("12.50")).value_or(-1), 1250LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("12,5")).value_or(-1), 1250LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("12,50")).value_or(-1), 1250LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("0,5")).value_or(-1), 50LL);
    QCOMPARE(ui::parseMoney(QStringLiteral(".5")).value_or(-1), 50LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("1,000")).value_or(-1), 100000LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("1.000")).value_or(-1), 100000LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("1,234.50")).value_or(-1), 123450LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("12.75")).value_or(-1), 1275LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("500 دج")).value_or(-1), 50000LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("1 234")).value_or(-1), 123400LL);
    QCOMPARE(ui::parseMoney(QStringLiteral("١٢٫٥٠")).value_or(-1), 1250LL);
    QVERIFY(!ui::parseMoney(QString()).has_value());
    QVERIFY(!ui::parseMoney(QStringLiteral("abc")).has_value());
    QVERIFY(!ui::parseMoney(QStringLiteral("-5")).has_value());
}

void UiTest::posSaleWithPriceOverride()
{
    int productId = 0;
    int sessionId = 0;
    const QString path = m_dir.filePath(QStringLiteral("pos-override.sqlite"));
    productId = seedProduct(path, &sessionId, 10000);

    data::Database db(path);
    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("6130000000004"));
    page.addEntry();
    QCOMPARE(page.lineCount(), 1);
    QCOMPARE(page.linePriceAt(0), 10000LL);
    QCOMPARE(page.totalCents(), 10000LL);

    // Cashier edits quantity and overrides the unit price in the grid.
    page.table()->item(0, 1)->setText(QStringLiteral("2"));
    page.table()->item(0, 2)->setText(QStringLiteral("7.50"));
    page.completeSale();

    QVERIFY(page.lastSaleId() != 0);
    QCOMPARE(page.lineCount(), 0);
    QVERIFY(page.noticeText().contains(QStringLiteral("تم البيع")));

    data::SaleRepository sales(db);
    const auto sale = sales.findById(page.lastSaleId());
    QVERIFY(sale.has_value());
    QCOMPARE(sale->totalCents, 1500LL);
    QCOMPARE(sale->deviceId, QStringLiteral("desktop"));

    data::SaleItemRepository saleItems(db);
    const auto items = saleItems.findBySaleId(page.lastSaleId());
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].unitPriceCents, 750LL);
    QCOMPARE(items[0].quantity, 2LL);

    data::ProductRepository products(db);
    QCOMPARE(products.findById(productId)->quantity, 98LL);

    data::CashMovementRepository movements(db);
    QCOMPARE(movements.sumBySessionId(sessionId), 1500LL);

    // Desktop sales never touch the sync outbox/applied ops.
    QCOMPARE(data::AppliedOpRepository(db).count(), 0);

    // The override left one audit trail entry.
    int overrides = 0;
    for (const auto& entry : data::AuditLogRepository(db).findAll()) {
        if (entry.action == QLatin1String("price_override")) {
            ++overrides;
        }
    }
    QCOMPARE(overrides, 1);
}

void UiTest::posSaleRequiresOpenSession()
{
    const QString path = m_dir.filePath(QStringLiteral("pos-no-session.sqlite"));
    int unusedSession = 0;
    const int productId = seedProduct(path, &unusedSession, 8000);

    data::Database db(path);
    data::CashSessionRepository sessions(db);
    const auto open = sessions.findOpen();
    if (open.has_value()) {
        sessions.close(open->id, open->openingFloatCents, open->openingFloatCents, 0);
    }

    ui::PosPage page(db);
    page.setEntryText(QStringLiteral("6130000000004"));
    page.addEntry();
    QCOMPARE(page.lineCount(), 1);
    page.completeSale();
    QCOMPARE(page.lastSaleId(), 0);
    QCOMPARE(page.lineCount(), 1);
    QVERIFY(!page.noticeText().isEmpty());

    data::SaleRepository sales(db);
    QCOMPARE(static_cast<int>(sales.findAll().size()), 0);
}

void UiTest::cashSessionLifecycle()
{
    const QString path = m_dir.filePath(QStringLiteral("session.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 6000;
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    product.packageSize = 1;
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    ui::CashSessionPage page(db);
    QVERIFY(!page.hasOpenSession());

    page.openSession(5000);
    QVERIFY(page.hasOpenSession());
    QVERIFY(page.sessionId() > 0);
    QCOMPARE(page.movementCount(), 0);
    QCOMPARE(page.expectedCents(), 5000LL);

    data::SaleService service(db);
    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    const data::SaleRecordResult result = service.recordSale({ item }, page.sessionId(), "desktop", false);
    QVERIFY(result.ok);
    QCOMPARE(result.totalCents, 10000LL);

    page.refresh();
    QCOMPARE(page.movementCount(), 1);
    QCOMPARE(page.expectedCents(), 15000LL);

    page.closeSession(15000);
    QVERIFY(!page.hasOpenSession());
    QCOMPARE(page.lastVarianceCents(), 0LL);

    page.openSession(2000);
    item.quantity = 2;
    const data::SaleRecordResult result2 = service.recordSale({ item }, page.sessionId(), "desktop", false);
    QVERIFY(result2.ok);
    QCOMPARE(result2.totalCents, 10000LL);
    page.refresh();
    QCOMPARE(page.expectedCents(), 12000LL);
    page.closeSession(11500);
    QVERIFY(!page.hasOpenSession());
    QCOMPARE(page.lastVarianceCents(), -500LL);
}

void UiTest::salesPageShowsToday()
{
    int unused = 0;
    const QString path = m_dir.filePath(QStringLiteral("sales-today.sqlite"));
    const int productId = seedProduct(path, &unused, 10000);

    data::Database db(path);
    data::CashSessionRepository sessions(db);
    const auto session = sessions.findOpen();
    QVERIFY(session.has_value());
    data::SaleService service(db);

    core::SaleItem desktopItem;
    desktopItem.productId = productId;
    desktopItem.quantity = 2;
    desktopItem.unitPriceCents = 7500; // override
    QVERIFY(service.recordSale({ desktopItem }, session->id, "desktop", false).ok);

    core::SaleItem deviceItem;
    deviceItem.productId = productId;
    deviceItem.quantity = 1;
    QVERIFY(service.recordSale({ deviceItem }, session->id, "dev-1", false).ok);

    ui::SalesPage page(db);
    QCOMPARE(page.rowCount(), 2);
    QCOMPARE(page.grandTotalCents(), 25000LL);
    QCOMPARE(page.profitCents(), 7000LL);
}

// grandTotalCents()/profitCents() used to scrape the numbers back out of the Arabic
// summary label with a regex, so a translated UI silently reported the grand total
// as the profit. Overwrite the label with non-Arabic text and both must hold steady.
void UiTest::profit_cents_survives_translation()
{
    const QString path = m_dir.filePath(QStringLiteral("sales-translated.sqlite"));
    int seededSessionId = 0;
    const int productId = seedProduct(path, &seededSessionId, 10000);
    QVERIFY(productId > 0);

    data::Database db(path);
    data::CashSessionRepository sessions(db);
    const auto session = sessions.findOpen();
    QVERIFY(session.has_value());
    data::SaleService service(db);

    core::SaleItem desktopItem;
    desktopItem.productId = productId;
    desktopItem.quantity = 2;
    desktopItem.unitPriceCents = 7500; // override
    QVERIFY(service.recordSale({ desktopItem }, session->id, "desktop", false).ok);

    core::SaleItem deviceItem;
    deviceItem.productId = productId;
    deviceItem.quantity = 1;
    deviceItem.unitPriceCents = 10000;
    QVERIFY(service.recordSale({ deviceItem }, session->id, "dev-1", false).ok);

    ui::SalesPage page(db);
    const qint64 total = page.grandTotalCents();
    const qint64 profit = page.profitCents();
    QCOMPARE(total, 25000LL);
    QCOMPARE(profit, 7000LL);
    QVERIFY(total != profit); // otherwise the old fallback would be indistinguishable

    QLabel* summary = page.findChild<QLabel*>(QStringLiteral("infoBar"));
    QVERIFY(summary);
    summary->setText(QStringLiteral("Ventes: 2  |  Total (net): 999.99  |  Benefice: 888.88"));

    QCOMPARE(page.grandTotalCents(), total);
    QCOMPARE(page.profitCents(), profit);

    // A refresh re-renders the label in the source language; the cache must be rebuilt
    // to the same figures.
    page.refresh();
    QCOMPARE(page.grandTotalCents(), total);
    QCOMPARE(page.profitCents(), profit);
}

// A barcode scanner terminates every scan with Enter. In a QDialog that Enter used
// to hit the default (OK) button, saving and closing the form mid-entry.
void UiTest::barcode_dialog_does_not_close()
{
    const QString path = m_dir.filePath(QStringLiteral("barcode-dialog.sqlite"));
    data::Database db(path);

    app::ui::ProductsPage page(db);
    page.refresh();

    QPushButton* addBtn = nullptr;
    for (QPushButton* b : page.findChildren<QPushButton*>()) {
        if (b->text() == QStringLiteral("إضافة منتج")) {
            addBtn = b;
            break;
        }
    }
    QVERIFY2(addBtn, "could not find the add-product button");

    bool modalOpened = false;
    bool stillOpen = false;
    bool focusMovedToName = false;
    bool barcodeHeldScan = false;
    bool watchdogFired = false;

    // productDialog() calls exec(), so drive it from a 0 ms timer that runs inside
    // the dialog's nested event loop.
    QTimer::singleShot(0, [&]() {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) {
            return;
        }
        modalOpened = true;

        // The fields are addressed through their form labels: the dialog sets no
        // placeholder text, and relying on child order would be brittle.
        QFormLayout* form = nullptr;
        for (QFormLayout* l : modal->findChildren<QFormLayout*>()) {
            form = l;
            break;
        }
        if (!form) {
            return;
        }

        QLineEdit* barcodeField = nullptr;
        QLineEdit* nameField = nullptr;
        for (int row = 0; row < form->rowCount(); ++row) {
            QLayoutItem* labelItem = form->itemAt(row, QFormLayout::LabelRole);
            QLayoutItem* fieldItem = form->itemAt(row, QFormLayout::FieldRole);
            auto* label = qobject_cast<QLabel*>(labelItem ? labelItem->widget() : nullptr);
            auto* field = qobject_cast<QLineEdit*>(fieldItem ? fieldItem->widget() : nullptr);
            if (!label || !field) {
                continue;
            }
            if (label->text() == QStringLiteral("الباركود")) {
                barcodeField = field;
            } else if (label->text() == QStringLiteral("الاسم")) {
                nameField = field;
            }
        }
        if (!barcodeField || !nameField) {
            return;
        }

        barcodeField->setFocus();
        QTest::keyClicks(barcodeField, QStringLiteral("12345"));
        // Same event a scanner emits: the digits, then Return.
        QTest::keyClick(barcodeField, Qt::Key_Return);
        QTest::qWait(20);

        stillOpen = modal->isVisible();
        barcodeHeldScan = (barcodeField->text() == QStringLiteral("12345"));
        focusMovedToName = (modal->focusWidget() == nameField);

        modal->reject();
    });

    // Safety net: a stuck modal must never hang the whole suite.
    QTimer::singleShot(3000, [&]() {
        watchdogFired = true;
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            modal->reject();
        }
    });

    addBtn->click();

    QVERIFY2(!watchdogFired, "the product dialog had to be closed by the watchdog");
    QVERIFY2(modalOpened, "the add-product button did not open the product dialog");
    QVERIFY2(barcodeHeldScan, "the barcode field lost the scanned digits");
    QVERIFY2(stillOpen, "Enter from the barcode scanner closed the product dialog");
    QVERIFY2(focusMovedToName, "Enter did not move focus from the barcode to the name field");
}

void UiTest::customerCreditAndPayment()
{
    const QString path = m_dir.filePath(QStringLiteral("customer-credit.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 6000;
    product.salePriceCents = 10000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("زبون آجل");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);

    ui::CustomersPage page(db);

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    item.unitPriceCents = 5000; // override: credit at a lower price
    page.recordDebt(customerId, { item });

    QVERIFY(page.noticeText().contains(QStringLiteral("دين")));
    QCOMPARE(page.balanceAt(0), QStringLiteral("100.00"));

    // Credit sales never touch the till and never enter the sync outbox.
    data::CashMovementRepository movements(db);
    QCOMPARE(movements.sumBySessionId(sessionId), 0LL);
    QCOMPARE(data::AppliedOpRepository(db).count(), 0);

    page.recordPayment(customerId, 4000, QString());
    QVERIFY(page.noticeText().contains(QStringLiteral("سداد")));
    QCOMPARE(page.balanceAt(0), QStringLiteral("60.00"));
    QCOMPARE(page.balanceAt(0), QStringLiteral("60.00"));
    Q_UNUSED(db)
    QCOMPARE(movements.sumBySessionId(sessionId), 4000LL);

    bool foundPayment = false;
    for (const core::CashMovement& movement : movements.findBySessionId(sessionId)) {
        if (movement.type == QLatin1String("customer_payment")) {
            foundPayment = true;
            QCOMPARE(movement.amountCents, 4000LL);
        }
    }
    QVERIFY(foundPayment);
}

void UiTest::expensesAndDrawings()
{
    const QString path = m_dir.filePath(QStringLiteral("cash-entries.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    ui::ExpensesPage page(db);
    page.recordExpense(QStringLiteral("كهرباء"), 1500);
    QVERIFY(page.noticeText().contains(QStringLiteral("مصروف")));
    page.recordDrawing(QStringLiteral("سحب شخصي"), 2000);
    QVERIFY(page.noticeText().contains(QStringLiteral("سحب")));

    QCOMPARE(page.entryCount(), 2);
    QCOMPARE(page.expensesTotalCents(), 1500LL);

    data::ExpenseRepository expenses(db);
    const auto expenseRows = expenses.findBetween(QDateTime(QDate::currentDate(), QTime(0, 0, 0)),
                                                  QDateTime::currentDateTime());
    QCOMPARE(expenseRows.size(), 1);
    QCOMPARE(expenseRows[0].amountCents, 1500LL);

    data::OwnerDrawingRepository drawings(db);
    const auto drawingRows = drawings.findBetween(QDateTime(QDate::currentDate(), QTime(0, 0, 0)),
                                                  QDateTime::currentDateTime());
    QCOMPARE(drawingRows.size(), 1);
    QCOMPARE(drawingRows[0].amountCents, 2000LL);

    data::CashMovementRepository movements(db);
    QCOMPARE(movements.sumBySessionId(sessionId), -3500LL);
}

void UiTest::reportBuilds()
{
    const QString path = m_dir.filePath(QStringLiteral("report.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 3000;
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("زبون");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);

    data::SaleService saleService(db);
    core::SaleItem saleItem;
    saleItem.productId = productId;
    saleItem.quantity = 2;
    QVERIFY(saleService.recordSale({ saleItem }, sessionId, "desktop", false).ok);

    core::SaleItem debtItem;
    debtItem.productId = productId;
    debtItem.quantity = 1;
    QVERIFY(saleService.recordCustomerDebt(customerId, { debtItem }, "desktop", false).ok);

    data::PaymentService payments(db);
    QVERIFY(payments.recordCustomerPayment(customerId, 2000, sessionId, QString()).ok);

    data::CashEntryService cashEntries(db);
    QVERIFY(cashEntries.recordExpense(QStringLiteral("كهرباء"), 1500, sessionId).ok);
    QVERIFY(cashEntries.recordDrawing(QStringLiteral("سحب"), 2000, sessionId).ok);

    const QDateTime dayStart(QDate::currentDate(), QTime(0, 0, 0));
    const data::StoreReport report = data::ReportService(db).build(dayStart, QDateTime::currentDateTime());

    QCOMPARE(report.revenueCents, 10000LL);
    QCOMPARE(report.salesCount, 1LL);
    QCOMPARE(report.cogsCents, 6000LL);
    QCOMPARE(report.grossProfitCents, 4000LL);
    QCOMPARE(report.expensesCents, 1500LL);
    QCOMPARE(report.drawingsCents, 2000LL);
    QCOMPARE(report.netProfitCents, 2500LL);
    QCOMPARE(report.outstandingDebtCents, 3000LL);
    QCOMPARE(report.zakatBaseCents, 13000LL);
    QCOMPARE(report.zakatCents, 325LL);
    QCOMPARE(report.sessionsOpened, 1LL);
    QCOMPARE(report.openingFloatCents, 5000LL);

    const auto findLine = [&report](const QString& type) -> const data::CashLine* {
        for (const data::CashLine& line : report.cashLines) {
            if (line.type == type) {
                return &line;
            }
        }
        return nullptr;
    };
    const data::CashLine* saleLine = findLine(QStringLiteral("sale"));
    QVERIFY(saleLine != nullptr);
    QCOMPARE(saleLine->count, 1);
    QCOMPARE(saleLine->sumCents, 10000LL);
    QCOMPARE(findLine(QStringLiteral("customer_payment"))->sumCents, 2000LL);
    QCOMPARE(findLine(QStringLiteral("expense"))->sumCents, -1500LL);
    QCOMPARE(findLine(QStringLiteral("drawing"))->sumCents, -2000LL);

    ui::ReportsPage page(db);
    QCOMPARE(page.report().revenueCents, 10000LL);
    QCOMPARE(page.report().zakatCents, 325LL);
    QCOMPARE(page.report().netProfitCents, 2500LL);
}

void UiTest::settingsCurrencyAndZakat()
{
    const QString path = m_dir.filePath(QStringLiteral("settings.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    ui::SettingsPage page(db);

    page.setShopName(QStringLiteral("محل النور"));
    page.setCurrencySymbol(QStringLiteral("دج"));
    page.setZakatEnabled(true);
    page.setSyncKey(QStringLiteral("my-sync-key"));
    page.save();

    QVERIFY(page.noticeText().contains(QStringLiteral("حُفظت")));
    QVERIFY(QStringLiteral("15.00 دج") == ui::formatMoney(1500));

    data::SettingRepository settings(db);
    QCOMPARE(settings.value(QStringLiteral("shop_name")).value_or(QString()), QStringLiteral("محل النور"));
    QCOMPARE(settings.value(QStringLiteral("currency_symbol")).value_or(QString()), QStringLiteral("دج"));
    QCOMPARE(settings.value(QStringLiteral("sync_hmac_key")).value_or(QString()), QStringLiteral("my-sync-key"));
    data::ZakatSettingRepository zakat(db);
    const auto enabledRow = zakat.findByKey(QStringLiteral("enabled"));
    QVERIFY(enabledRow.has_value());
    QCOMPARE(enabledRow->value, QStringLiteral("1"));

    // Disabled zakat in the settings must zero out the report's zakat line.
    page.setZakatEnabled(false);
    page.save();
    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);
    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 3000;
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 10, QStringLiteral("purchase"));

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    QVERIFY(data::SaleService(db).recordSale({ item }, sessionId, "desktop", false).ok);

    const QDateTime dayStart(QDate::currentDate(), QTime(0, 0, 0));
    const data::StoreReport report = data::ReportService(db).build(dayStart, QDateTime::currentDateTime());
    QCOMPARE(report.zakatBaseCents, 10000LL);
    QCOMPARE(report.zakatCents, 0LL);

    app::ui::setCurrencySymbol(QString());
}

void UiTest::refundsRestoreMoneyAndStock()
{
    const QString path = m_dir.filePath(QStringLiteral("refunds.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 3000;
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("زبون");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);
    Q_UNUSED(customerId);

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 2;
    const data::SaleRecordResult sale =
        data::SaleService(db).recordSale({ item }, sessionId, "desktop", false);
    QVERIFY(sale.ok);
    QCOMPARE(products.findById(productId)->quantity, 98);

    ui::RefundsPage page(db);
    QCOMPARE(page.salesRowCount(), 1);
    page.refundSale(sale.saleId);
    QVERIFY(page.noticeText().contains(QStringLiteral("استرداد")));
    QCOMPARE(page.salesRowCount(), 0);
    QCOMPARE(products.findById(productId)->quantity, 100);

    data::CashMovementRepository movements(db);
    QCOMPARE(movements.sumBySessionId(sessionId), 0LL);
    bool foundRefund = false;
    for (const core::CashMovement& movement : movements.findBySessionId(sessionId)) {
        if (movement.type == QLatin1String("refund")) {
            foundRefund = true;
            QCOMPARE(movement.amountCents, -10000LL);
        }
    }
    QVERIFY(foundRefund);

    const auto reversals = data::SaleRepository(db).findBetween(
        QDateTime(QDate::currentDate(), QTime(0, 0, 0)), QDateTime::currentDateTime());
    QCOMPARE(static_cast<long long>(reversals.size()), 2);
    const core::Sale& reversal = reversals[0].reversedSaleId != 0 ? reversals[0] : reversals[1];
    const core::Sale& original = reversals[0].reversedSaleId == 0 ? reversals[0] : reversals[1];
    QCOMPARE(reversal.totalCents, -original.totalCents);
    QCOMPARE(reversal.reversedSaleId, sale.saleId);

    data::AuditLogRepository audit(db);
    const auto entries = audit.findBetween(QDateTime(QDate::currentDate(), QTime(0, 0, 0)),
                                           QDateTime::currentDateTime());
    bool foundSaleRefund = false;
    for (const core::AuditLogEntry& entry : entries) {
        if (entry.action == QLatin1String("sale_refund")) {
            foundSaleRefund = true;
        }
    }
    QVERIFY(foundSaleRefund);
}

void UiTest::paymentRefundRaisesBalance()
{
    const QString path = m_dir.filePath(QStringLiteral("payment-refund.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::ProductRepository products(db);
    core::Product product;
    product.barcode = QStringLiteral("6130000000004");
    product.name = QStringLiteral("معجون");
    product.costPriceCents = 3000;
    product.salePriceCents = 5000;
    product.unit = QStringLiteral("أنبوب");
    const int productId = products.save(product);
    QVERIFY(productId > 0);
    products.adjustStock(productId, 100, QStringLiteral("purchase"));

    data::CustomerRepository customers(db);
    core::Customer customer;
    customer.name = QStringLiteral("زبون");
    const int customerId = customers.save(customer);
    QVERIFY(customerId > 0);

    core::SaleItem item;
    item.productId = productId;
    item.quantity = 1;
    QVERIFY(data::SaleService(db).recordCustomerDebt(customerId, { item }, "desktop", false).ok);

    data::PaymentService payments(db);
    const data::PaymentResult paid = payments.recordCustomerPayment(customerId, 2000, sessionId, QString());
    QVERIFY(paid.ok);

    ui::RefundsPage page(db);
    QCOMPARE(page.paymentsRowCount(), 1);
    page.refundPayment(paid.paymentId, QStringLiteral("خطأ في المبلغ"));
    QVERIFY(page.noticeText().contains(QStringLiteral("استرداد")));
    QCOMPARE(page.paymentsRowCount(), 0);

    // Balance goes back to the full debt: 5000 - 2000 + 2000 = 5000.
    ui::CustomersPage customersPage(db);
    QCOMPARE(customersPage.balanceAt(0), QStringLiteral("50.00"));

    data::CashMovementRepository movements(db);
    QCOMPARE(movements.sumBySessionId(sessionId), 0LL);

    data::AuditLogRepository audit(db);
    const auto entries = audit.findBetween(QDateTime(QDate::currentDate(), QTime(0, 0, 0)),
                                           QDateTime::currentDateTime());
    bool foundRefund = false;
    for (const core::AuditLogEntry& entry : entries) {
        if (entry.action == QLatin1String("customer_payment_refund")) {
            foundRefund = true;
        }
    }
    QVERIFY(foundRefund);
}

void UiTest::entryReversal()
{
    const QString path = m_dir.filePath(QStringLiteral("entry-reversal.sqlite"));
    QFile::remove(path);
    data::Database db(path);

    data::CashSessionRepository sessions(db);
    const int sessionId = sessions.open(5000);
    QVERIFY(sessionId > 0);

    data::CashEntryService cash(db);
    QVERIFY(cash.recordExpense(QStringLiteral("كهرباء"), 1500, sessionId).ok);
    const data::CashEntryResult drawing = cash.recordDrawing(QStringLiteral("سحب"), 2000, sessionId);
    QVERIFY(drawing.ok);

    ui::ExpensesPage page(db);
    QCOMPARE(page.entryCount(), 2);

    // The page's per-row reverse undoes one expense: refund movement back in.
    page.reverseRow(0);
    QCOMPARE(data::CashMovementRepository(db).sumBySessionId(sessionId), -2000LL);

    const QDateTime dayStart(QDate::currentDate(), QTime(0, 0, 0));
    const auto expenses = data::ExpenseRepository(db).findBetween(dayStart, QDateTime::currentDateTime());
    QCOMPARE(static_cast<long long>(expenses.size()), 2);
    long long expenseSum = 0;
    bool hasReversal = false;
    for (const core::Expense& expense : expenses) {
        expenseSum += expense.amountCents;
        if (expense.amountCents < 0) {
            hasReversal = true;
        }
    }
    QCOMPARE(expenseSum, 0LL);
    QVERIFY(hasReversal);

    QVERIFY(cash.reverseDrawing(drawing.entryId, sessionId).ok);
    QCOMPARE(data::CashMovementRepository(db).sumBySessionId(sessionId), 0LL);

    data::AuditLogRepository audit(db);
    const auto entries = audit.findBetween(dayStart, QDateTime::currentDateTime());
    QCOMPARE(static_cast<long long>(entries.size()), 1);
    QCOMPARE(entries[0].action, QStringLiteral("entry_reversal"));
}

void UiTest::login_basic()
{
    const QString path = m_dir.filePath(QStringLiteral("login_test.sqlite"));
    QFile::remove(path);
    data::Database db(path);
    data::UserRepository repo(db);

    core::User user;
    user.name = QStringLiteral("اختبار");
    user.role = QStringLiteral("cashier");
    const int id = repo.save(user);
    QVERIFY(id > 0);
    QVERIFY(repo.savePin(id, QStringLiteral("12")));

    const auto found = repo.findByPin(QStringLiteral("12"));
    QVERIFY(found.has_value());
    QCOMPARE(found->id, id);
    QCOMPARE(found->name, QStringLiteral("اختبار"));

    app::core::Session::instance().setCurrentUser(*found);
    QVERIFY(app::core::Session::instance().hasUser());
    QCOMPARE(app::core::Session::instance().actorName(), QStringLiteral("اختبار"));

    app::core::Session::instance().clear();
    QVERIFY(!app::core::Session::instance().hasUser());
    QCOMPARE(app::core::Session::instance().actorName(), QStringLiteral("desktop"));
}

// void UiTest::statusBarSwitchUserButton()
// {
//     const QString path = m_dir.filePath(QStringLiteral("statusbar_test.sqlite"));
//     QFile::remove(path);
//     data::Database db(path, data::DatabaseMode::Server);
//     app::ui::ServerController controller(db, m_key);
//     controller.start();
//
//     app::ui::MainWindow window(db, controller);
//     window.show();
//
//     // Find the "تبديل المستخدم" button by text
//     auto* switchBtn = window.findChild<QPushButton*>();
//     QVERIFY(switchBtn);
//     while (switchBtn && switchBtn->text() != QStringLiteral("تبديل المستخدم")) {
//         auto buttons = window.findChildren<QPushButton*>();
//         bool found = false;
//         for (auto* btn : buttons) {
//             if (btn->text() == QStringLiteral("تبديل المستخدم")) {
//                 switchBtn = btn;
//                 found = true;
//                 break;
//             }
//         }
//         QVERIFY(found);
//     }
//     QVERIFY(switchBtn->isVisible());
//     QCOMPARE(switchBtn->text(), QStringLiteral("تبديل المستخدم"));
//
//     // Find the user label
//     auto* userLabel = window.findChild<QLabel*>(QStringLiteral("userLabel"));
//     QVERIFY(userLabel);
//     QVERIFY(userLabel->isVisible());
//     QVERIFY(userLabel->text().startsWith(QStringLiteral("المستخدم: ")));
//
//     window.close();
// }

void UiTest::users_page_lists_users()
{
    const QString path = m_dir.filePath(QStringLiteral("users_page_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);

    // Create admin user
    data::UserRepository userRepo(db);
    core::User admin;
    admin.name = QStringLiteral("المدير");
    admin.role = QStringLiteral("admin");
    const int adminId = userRepo.save(admin);
    QVERIFY(adminId > 0);
    QVERIFY(userRepo.savePin(adminId, QStringLiteral("12")));

    // Create cashier user
    core::User cashier;
    cashier.name = QStringLiteral("كاشير1");
    cashier.role = QStringLiteral("cashier");
    const int cashierId = userRepo.save(cashier);
    QVERIFY(cashierId > 0);
    QVERIFY(userRepo.savePin(cashierId, QStringLiteral("34")));

    // Test UsersPage directly without MainWindow
    app::ui::UsersPage page(db);
    page.refresh();
    QCOMPARE(page.rowCount(), 2);
}

void UiTest::users_page_shows_all_pins()
{
    const QString path = m_dir.filePath(QStringLiteral("users_page_pins_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    data::UserRepository userRepo(db);

    struct Seed {
        QString name;
        QString role;
        QString pin;
    };
    const QVector<Seed> seeds = {
        {QStringLiteral("المدير"), QStringLiteral("admin"), QStringLiteral("12")},
        {QStringLiteral("كاشير1"), QStringLiteral("cashier"), QStringLiteral("34")},
        {QStringLiteral("مدير2"), QStringLiteral("admin"), QStringLiteral("56")},
    };

    for (const Seed& seed : seeds) {
        core::User user;
        user.name = seed.name;
        user.role = seed.role;
        const int id = userRepo.save(user);
        QVERIFY(id > 0);
        QVERIFY(userRepo.savePin(id, seed.pin));
    }

    app::ui::UsersPage page(db);
    page.refresh();
    QCOMPARE(page.rowCount(), seeds.size());

    // Every row must show its own PIN, whatever the role.
    QTableWidget* table = page.table();
    QVERIFY(table);

    QHash<QString, QString> pinByName;
    for (int row = 0; row < table->rowCount(); ++row) {
        QTableWidgetItem* nameItem = table->item(row, 0);
        QTableWidgetItem* pinItem = table->item(row, 2);
        QVERIFY2(nameItem, qPrintable(QStringLiteral("missing name cell on row %1").arg(row)));
        QVERIFY2(pinItem, qPrintable(QStringLiteral("missing PIN cell on row %1").arg(row)));
        pinByName.insert(nameItem->text(), pinItem->text());
    }

    QCOMPARE(pinByName.size(), seeds.size());
    for (const Seed& seed : seeds) {
        QVERIFY2(pinByName.contains(seed.name),
                 qPrintable(QStringLiteral("no row for user %1").arg(seed.name)));
        QCOMPARE(pinByName.value(seed.name), seed.pin);
    }
}

// Drives the real modal that UsersPage::userDialog() opens, so the PIN the
// operator types is the value the production code has to carry back.
void UiTest::user_edit_pin_persists()
{
    const QString path = m_dir.filePath(QStringLiteral("user_edit_pin_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    data::UserRepository repo(db);

    core::User user;
    user.name = QStringLiteral("المدير");
    user.role = QStringLiteral("admin");
    const int id = repo.save(user);
    QVERIFY(id > 0);
    QVERIFY(repo.savePin(id, QStringLiteral("12")));

    app::ui::UsersPage page(db);
    page.refresh();

    QTableWidget* table = page.table();
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 1);

    QWidget* actions = table->cellWidget(0, 4);
    QVERIFY(actions);
    QPushButton* editBtn = nullptr;
    for (QPushButton* b : actions->findChildren<QPushButton*>()) {
        if (b->text() == QStringLiteral("تعديل")) {
            editBtn = b;
            break;
        }
    }
    QVERIFY2(editBtn, "could not find the تعديل button for row 0");

    // userDialog() calls exec(), which spins a nested event loop; run the
    // typing from a 0ms timer so it lands inside that loop.
    bool modalOpened = false;
    bool typedOk = false;
    QTimer::singleShot(0, [&]() {
        auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!modal) {
            return;
        }
        modalOpened = true;

        QLineEdit* pinField = nullptr;
        QLineEdit* confirmField = nullptr;
        for (QLineEdit* e : modal->findChildren<QLineEdit*>()) {
            const QString hint = e->placeholderText();
            if (hint == QStringLiteral("PIN (حرفان)")) {
                pinField = e;
            } else if (hint == QStringLiteral("تأكيد PIN")) {
                confirmField = e;
            }
        }
        if (!pinField || !confirmField) {
            return;
        }
        QTest::keyClicks(pinField, QStringLiteral("34"));
        QTest::keyClicks(confirmField, QStringLiteral("34"));
        typedOk = (pinField->text() == QStringLiteral("34")
                   && confirmField->text() == QStringLiteral("34"));

        auto* box = modal->findChild<QDialogButtonBox*>();
        if (box) {
            if (QPushButton* ok = box->button(QDialogButtonBox::Ok)) {
                ok->click();
            }
        }
    });

    editBtn->click();

    QVERIFY2(modalOpened, "editing a user did not open the PIN dialog");
    QVERIFY2(typedOk, "could not type the new PIN into the dialog");

    // The new PIN must be the one stored, and the old one must stop working.
    const auto byNewPin = repo.findByPin(QStringLiteral("34"));
    QVERIFY2(byNewPin.has_value(), "the edited PIN 34 was not persisted");
    QCOMPARE(byNewPin->id, id);

    const auto byOldPin = repo.findByPin(QStringLiteral("12"));
    QVERIFY2(!byOldPin.has_value(), "the old PIN 12 still grants access after the edit");

    // And the users table must now render the new PIN.
    page.refresh();
    QTableWidgetItem* nameItem = table->item(0, 0);
    QTableWidgetItem* pinItem = table->item(0, 2);
    QVERIFY(nameItem);
    QVERIFY(pinItem);
    QCOMPARE(nameItem->text(), QStringLiteral("المدير"));
    QCOMPARE(pinItem->text(), QStringLiteral("34"));
}

void UiTest::master_recovery_grants_access()
{
    const QString path = m_dir.filePath(QStringLiteral("master_recovery_test.sqlite"));
    QFile::remove(path);
    data::Database db(path, data::DatabaseMode::Server);
    data::UserRepository repo(db);

    core::User admin;
    admin.name = QStringLiteral("المدير");
    admin.role = QStringLiteral("admin");
    const int adminId = repo.save(admin);
    QVERIFY(adminId > 0);

    const QString masterWord = QStringLiteral("swordfish-42");
    data::AdminSecretRepository secrets(db);
    QVERIFY(secrets.setMaster(adminId, masterWord));
    QVERIFY(secrets.verifyMaster(adminId, masterWord));

    app::core::Session::instance().clear();

    app::ui::LoginDialog dialog(db);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));

    auto* input = dialog.findChild<QLineEdit*>(QStringLiteral("recoveryInput"));
    QVERIFY(input);

    // A wrong recovery word must not grant access.
    input->setText(QStringLiteral("wrong-word"));
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onRecoveryReturnPressed",
                                      Qt::DirectConnection));
    QVERIFY2(!app::core::Session::instance().hasUser(),
             "a wrong recovery word granted access");

    // The reported symptom: eight header clicks must reveal a field the
    // operator can actually type into.
    for (int i = 0; i < 8; ++i) {
        QVERIFY(QMetaObject::invokeMethod(&dialog, "onHeaderClicked", Qt::DirectConnection));
    }
    QVERIFY2(input->isVisible(), "the recovery field stayed invisible after 8 header clicks");
    QVERIFY2(!input->isHidden(), "the recovery field is explicitly hidden after 8 header clicks");

    // The correct recovery word must sign the admin in.
    input->setText(masterWord);
    QVERIFY(QMetaObject::invokeMethod(&dialog, "onRecoveryReturnPressed",
                                      Qt::DirectConnection));
    QVERIFY2(app::core::Session::instance().hasUser(),
             "the correct recovery word did not grant access");
    QCOMPARE(app::core::Session::instance().currentUser().id, adminId);
    QCOMPARE(app::core::Session::instance().actorName(), QStringLiteral("المدير"));

    app::core::Session::instance().clear();
    QVERIFY(!app::core::Session::instance().hasUser());
}

QTEST_MAIN(UiTest)
#include "tst_ui.moc"