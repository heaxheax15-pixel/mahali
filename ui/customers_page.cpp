#include "customers_page.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/customer_transaction.h"
#include "core/payment.h"
#include "core/sale_item.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/customer_transaction_repository.h"
#include "data/payment_repository.h"
#include "data/payment_service.h"
#include "data/product_repository.h"
#include "data/sale_service.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"
#include "format_utils.h"

namespace app::ui {

namespace {

std::optional<core::Customer> customerDialog(QWidget* parent, bool forNew, const core::Customer& initial)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(forNew ? QCoreApplication::translate("app::ui::CustomersPage", "عميل جديد")
                                 : QCoreApplication::translate("app::ui::CustomersPage", "تعديل العميل"));
    dialog.setModal(true);

    auto* name = new QLineEdit(initial.name);
    auto* phone = new QLineEdit(initial.phone);

    QFormLayout* form = new QFormLayout;
    form->addRow(QCoreApplication::translate("app::ui::CustomersPage", "الاسم"), name);
    form->addRow(QCoreApplication::translate("app::ui::CustomersPage", "الهاتف"), phone);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    if (name->text().trimmed().isEmpty()) {
        return std::nullopt;
    }
    core::Customer customer = initial;
    customer.name = name->text().trimmed();
    customer.phone = phone->text().trimmed();
    return customer;
}

long long balanceFor(app::data::Database& db, int customerId)
{
    // Money the customer owes = credit purchases minus payments (reversals are
    // recorded as negative rows, so summing handles refunds automatically).
    app::data::CustomerTransactionRepository transactions(db);
    long long balance = 0;
    for (const core::CustomerTransaction& tx : transactions.findByCustomerId(customerId)) {
        balance += tx.amountCents;
    }
    app::data::PaymentRepository payments(db);
    for (const core::Payment& payment : payments.findByCustomerId(customerId)) {
        balance -= payment.amountCents;
    }
    return balance;
}

// A compact credit-sale builder: pick products, set quantity/price, and return
// the resulting sale items (prices resolved to cents) on accept.
bool collectDebtItems(QWidget* parent, app::data::Database& db, QVector<core::SaleItem>* out)
{
    app::data::ProductRepository products(db);
    const auto catalog = products.findAll();

    QDialog dialog(parent);
    dialog.setWindowTitle(QCoreApplication::translate("app::ui::CustomersPage", "بيع آجل"));
    dialog.setModal(true);
    dialog.resize(560, 400);

    auto* combo = new QComboBox;
    combo->setEditable(true);
    for (const core::Product& product : catalog) {
        const QString label = product.barcode.isEmpty() ? product.name
                                                        : QStringLiteral("%1 (%2)").arg(product.name, product.barcode);
        combo->addItem(label, product.id);
        if (product.barcode == QLatin1String("6130000000004") && catalog.size() == 1) {
            combo->setCurrentIndex(combo->count() - 1);
        }
    }
    auto* qty = new QSpinBox;
    qty->setRange(1, 1000000);
    qty->setValue(1);
    auto* price = new QLineEdit;
    price->setPlaceholderText(QCoreApplication::translate("app::ui::CustomersPage", "اضغط لتغيير سعر/كغ"));
    auto* addButton = new QPushButton(QCoreApplication::translate("app::ui::CustomersPage", "أضف سطر"));
    auto* removeButton = new QPushButton(QCoreApplication::translate("app::ui::CustomersPage", "حذف السطر المحدد"));

    auto* table = new QTableWidget;
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels(
        {QCoreApplication::translate("app::ui::CustomersPage", "المنتج"),
         QCoreApplication::translate("app::ui::CustomersPage", "الكمية"),
         QCoreApplication::translate("app::ui::CustomersPage", "سعر الوحدة"),
         QCoreApplication::translate("app::ui::CustomersPage", "الإجمالي")});
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);

    auto* totalLabel = new QLabel;
    auto* hintLabel = new QLabel(
        QCoreApplication::translate("app::ui::CustomersPage", "الكمية والسعر عشري؟ عدّل في الأسطر قبل الحفظ."));

    auto* picker = new QHBoxLayout;
    picker->addWidget(combo, 1);
    picker->addWidget(qty);
    picker->addWidget(price);
    picker->addWidget(addButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)
        ->setText(QCoreApplication::translate("app::ui::CustomersPage", "حفظ البيع الآجل"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(picker);
    layout->addWidget(table);
    layout->addWidget(totalLabel);
    layout->addWidget(removeButton);
    layout->addWidget(hintLabel);
    layout->addWidget(buttons);

    auto refreshTotal = [table, totalLabel]() {
        long long total = 0;
        for (int i = 0; i < table->rowCount(); ++i) {
            total += parseMoney(table->item(i, 3)->text()).value_or(0);
        }
        totalLabel->setText(
            QCoreApplication::translate("app::ui::CustomersPage", "الإجمالي: %1").arg(formatMoney(total)));
    };

    QObject::connect(addButton, &QPushButton::clicked, &dialog, [&]() {
        const int productId = combo->currentData().toInt();
        if (productId <= 0) {
            QMessageBox::warning(
                &dialog, QCoreApplication::translate("app::ui::CustomersPage", "خطأ"),
                QCoreApplication::translate("app::ui::CustomersPage", "اختر منتجاً من القائمة"));
            return;
        }
        const auto product = products.findById(productId);
        if (!product) {
            return;
        }
        long long unitPrice = product->salePriceCents;
        if (!price->text().trimmed().isEmpty()) {
            const auto overridePrice = parseMoney(price->text());
            if (!overridePrice || *overridePrice < 0) {
                QMessageBox::warning(
                    &dialog, QCoreApplication::translate("app::ui::CustomersPage", "خطأ"),
                    QCoreApplication::translate("app::ui::CustomersPage", "السعر غير صالح"));
                return;
            }
            unitPrice = *overridePrice;
        }
        const int row = table->rowCount();
        table->insertRow(row);
        auto* nameItem = new QTableWidgetItem(product->name);
        nameItem->setData(Qt::UserRole, productId);
        table->setItem(row, 0, nameItem);
        table->setItem(row, 1, new QTableWidgetItem(QString::number(qty->value())));
        table->setItem(row, 2, new QTableWidgetItem(formatMoney(unitPrice)));
        table->setItem(row, 3, new QTableWidgetItem(formatMoney(unitPrice * qty->value())));
        price->clear();
        refreshTotal();
    });

    QObject::connect(removeButton, &QPushButton::clicked, &dialog, [&]() {
        const int row = table->currentRow();
        if (row >= 0) {
            table->removeRow(row);
            refreshTotal();
        }
    });

    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    out->clear();
    for (int i = 0; i < table->rowCount(); ++i) {
        bool qtyOk = false;
        const qlonglong quantity = table->item(i, 1)->text().toLongLong(&qtyOk);
        const auto unitPrice = parseMoney(table->item(i, 2)->text());
        if (!qtyOk || quantity <= 0 || !unitPrice || *unitPrice < 0) {
            return false;
        }
        core::SaleItem item;
        item.productId = table->item(i, 0)->data(Qt::UserRole).toInt();
        item.quantity = quantity;
        item.unitPriceCents = *unitPrice;
        out->append(item);
    }
    return !out->isEmpty();
}

} // namespace

CustomersPage::CustomersPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* add = new QPushButton(tr("إضافة عميل"));
    add->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    m_edit = new QPushButton(tr("تعديل"));
    m_edit->setObjectName(QStringLiteral("secondary"));
    m_debt = new QPushButton(tr("بيع آجل"));
    m_debt->setObjectName(QStringLiteral("secondary"));
    m_pay = new QPushButton(tr("سداد"));
    m_pay->setObjectName(QStringLiteral("secondary"));
    m_edit->setEnabled(false);
    m_debt->setEnabled(false);
    m_pay->setEnabled(false);

    m_notice = new QLabel;
    m_notice->setWordWrap(true);
    m_notice->setMinimumHeight(42);
    m_notice->setObjectName(QStringLiteral("noticeOk"));

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("customerTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(false);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {tr("الاسم"), tr("الهاتف"), tr("المطلوب (رصيد)")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setDefaultSectionSize(42);

    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(10);
    toolbar->addStretch(1);
    toolbar->addWidget(add);
    toolbar->addWidget(m_edit);
    toolbar->addWidget(m_debt);
    toolbar->addWidget(m_pay);

    auto* card = makeCard();
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(12);
    cardLayout->addWidget(makeCardTitle(tr("العملاء والرصيد")));
    cardLayout->addLayout(toolbar);
    cardLayout->addWidget(m_table, 1);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(tr("العملاء"),
                                   tr("المبيعات الآجلة وسداد الأرصدة")));
    root->addWidget(card, 1);
    root->addWidget(m_notice);

    connect(add, &QPushButton::clicked, this, &CustomersPage::onAddClicked);
    connect(m_edit, &QPushButton::clicked, this, &CustomersPage::onEditClicked);
    connect(m_debt, &QPushButton::clicked, this, &CustomersPage::onDebtClicked);
    connect(m_pay, &QPushButton::clicked, this, &CustomersPage::onPaymentClicked);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &CustomersPage::onSelectionChanged);

    refresh();
}

void CustomersPage::refresh()
{
    data::CustomerRepository customers(m_db);
    const auto all = customers.findAll();

    m_table->setRowCount(0);
    for (const core::Customer& customer : all) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(customer.name));
        m_table->setItem(row, 1, new QTableWidgetItem(customer.phone));
        m_table->setItem(row, 2, new QTableWidgetItem(formatMoney(balanceFor(m_db, customer.id))));
        m_table->item(row, 0)->setData(Qt::UserRole, customer.id);
    }
    onSelectionChanged();
}

int CustomersPage::rowCount() const
{
    return m_table->rowCount();
}

QString CustomersPage::balanceAt(int row) const
{
    if (row < 0 || row >= m_table->rowCount()) {
        return QString();
    }
    return m_table->item(row, 2)->text();
}

QString CustomersPage::noticeText() const
{
    return m_notice->text();
}

int CustomersPage::selectedCustomerId() const
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return 0;
    }
    const QTableWidgetItem* item = m_table->item(row, 0);
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

void CustomersPage::onSelectionChanged()
{
    const bool has = selectedCustomerId() != 0;
    m_edit->setEnabled(has);
    m_debt->setEnabled(has);
    m_pay->setEnabled(has);
}

void CustomersPage::onAddClicked()
{
    const auto maybeCustomer = customerDialog(this, true, core::Customer{});
    if (!maybeCustomer) {
        return;
    }
    data::CustomerRepository customers(m_db);
    if (customers.save(*maybeCustomer) == 0) {
        QMessageBox::warning(this, tr("خطأ"), tr("تعذر حفظ العميل"));
        return;
    }
    refresh();
}

void CustomersPage::onEditClicked()
{
    const int id = selectedCustomerId();
    if (id == 0) {
        return;
    }
    data::CustomerRepository customers(m_db);
    const auto existing = customers.findById(id);
    if (!existing) {
        return;
    }
    const auto maybeCustomer = customerDialog(this, false, *existing);
    if (!maybeCustomer) {
        return;
    }
    customers.save(*maybeCustomer);
    refresh();
}

void CustomersPage::onDebtClicked()
{
    const int id = selectedCustomerId();
    if (id == 0) {
        return;
    }
    QVector<core::SaleItem> items;
    if (!collectDebtItems(this, m_db, &items)) {
        return;
    }
    recordDebt(id, items);
}

void CustomersPage::onPaymentClicked()
{
    const int id = selectedCustomerId();
    if (id == 0) {
        return;
    }
    bool ok = false;
    const QString text =
        QInputDialog::getText(this, tr("سداد"),
                              tr("المبلغ الذي دفعه العميل الآن:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return;
    }
    const auto cents = parseMoney(text);
    if (!cents || *cents <= 0) {
        QMessageBox::warning(this, tr("خطأ"), tr("المبلغ غير صالح"));
        return;
    }
    recordPayment(id, *cents, QString());
}

void CustomersPage::recordDebt(int customerId, const QVector<core::SaleItem>& items)
{
    m_notice->clear();
    if (customerId <= 0 || items.isEmpty()) {
        m_notice->setText(tr("لا يوجد بنود للبيع الآجل"));
        return;
    }
    data::SaleService service(m_db);
    const data::SaleRecordResult result =
        service.recordCustomerDebt(customerId, items, QStringLiteral("desktop"), /*allowOversold=*/false);
    if (!result.ok) {
        m_notice->setText(tr("تعذر تسجيل البيع الآجل: %1").arg(result.error));
        return;
    }
    m_notice->setText(tr("سُجّل دين: %1").arg(formatMoney(result.totalCents)));
    refresh();
}

void CustomersPage::recordPayment(int customerId, long long amountCents, const QString& note)
{
    m_notice->clear();
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(tr("لا توجد جلسة مفتوحة — افتح جلسة الصندوق أولاً"));
        return;
    }
    data::PaymentService service(m_db);
    const data::PaymentResult result =
        service.recordCustomerPayment(customerId, amountCents, session->id, note);
    if (!result.ok) {
        m_notice->setText(tr("تعذر تسجيل السداد: %1").arg(result.error));
        return;
    }
    m_notice->setText(tr("سُجّل سداد: %1").arg(formatMoney(result.amountCents)));
    refresh();
}

} // namespace app::ui