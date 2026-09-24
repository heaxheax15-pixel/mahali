#include "customers_page.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/customer_transaction.h"
#include "core/payment.h"
#include "data/customer_repository.h"
#include "data/customer_transaction_repository.h"
#include "data/payment_repository.h"
#include "format_utils.h"

namespace app::ui {

namespace {

std::optional<core::Customer> customerDialog(QWidget* parent, bool forNew, const core::Customer& initial)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(forNew ? QStringLiteral("عميل جديد") : QStringLiteral("تعديل العميل"));
    dialog.setModal(true);

    auto* name = new QLineEdit(initial.name);
    auto* phone = new QLineEdit(initial.phone);

    QFormLayout* form = new QFormLayout;
    form->addRow(QStringLiteral("الاسم"), name);
    form->addRow(QStringLiteral("الهاتف"), phone);

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

} // namespace

CustomersPage::CustomersPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* add = new QPushButton(QStringLiteral("إضافة عميل"));
    auto* edit = new QPushButton(QStringLiteral("تعديل"));
    edit->setEnabled(false);

    m_table = new QTableWidget;
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("الاسم"), QStringLiteral("الهاتف"), QStringLiteral("المطلوب (رصيد)")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);

    auto* toolbar = new QHBoxLayout;
    toolbar->addStretch(1);
    toolbar->addWidget(add);
    toolbar->addWidget(edit);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(toolbar);
    layout->addWidget(m_table);

    connect(add, &QPushButton::clicked, this, &CustomersPage::onAddClicked);
    connect(edit, &QPushButton::clicked, this, &CustomersPage::onEditClicked);
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
    // Selection drives an editable footer in a later accounting phase; for now
    // the page stays read-heavy.
}

void CustomersPage::onAddClicked()
{
    const auto maybeCustomer = customerDialog(this, true, core::Customer{});
    if (!maybeCustomer) {
        return;
    }
    data::CustomerRepository customers(m_db);
    if (customers.save(*maybeCustomer) == 0) {
        QMessageBox::warning(this, QStringLiteral("خطأ"), QStringLiteral("تعذر حفظ العميل"));
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

} // namespace app::ui