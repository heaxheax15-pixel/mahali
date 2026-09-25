#include "suppliers_page.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/supplier_transaction.h"
#include "data/supplier_repository.h"
#include "data/supplier_transaction_repository.h"
#include "format_utils.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

std::optional<core::Supplier> supplierDialog(QWidget* parent, bool forNew, const core::Supplier& initial)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(forNew ? QStringLiteral("مورد جديد") : QStringLiteral("تعديل المورد"));
    dialog.setModal(true);

    auto* name = new QLineEdit(initial.name);

    QFormLayout* form = new QFormLayout;
    form->addRow(QStringLiteral("الاسم"), name);

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
    core::Supplier supplier = initial;
    supplier.name = name->text().trimmed();
    return supplier;
}

struct InvoiceInput {
    long long amountCents = 0;
    QString note;
};

std::optional<InvoiceInput> invoiceDialog(QWidget* parent, const QString& supplierName)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("فاتورة آجلة — %1").arg(supplierName));
    dialog.setModal(true);

    auto* amount = new QLineEdit;
    amount->setPlaceholderText(QStringLiteral("مثال: 4500.50"));
    auto* note = new QLineEdit;

    QFormLayout* form = new QFormLayout;
    form->addRow(QStringLiteral("المبلغ"), amount);
    form->addRow(QStringLiteral("ملاحظة"), note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    const auto cents = parseMoney(amount->text());
    if (!cents) {
        return std::nullopt;
    }
    InvoiceInput input;
    input.amountCents = *cents;
    input.note = note->text().trimmed();
    return input;
}

} // namespace

SuppliersPage::SuppliersPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* add = new QPushButton(QStringLiteral("إضافة مورد"));
    add->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    auto* edit = new QPushButton(QStringLiteral("تعديل"));
    edit->setObjectName(QStringLiteral("secondary"));
    m_addInvoice = new QPushButton(QStringLiteral("فاتورة آجلة"));
    m_addInvoice->setObjectName(QStringLiteral("secondary"));
    m_addInvoice->setEnabled(false);

    m_suppliers = new QTableWidget;
    m_suppliers->setAlternatingRowColors(true);
    m_suppliers->setFrameShape(QFrame::NoFrame);
    m_suppliers->setShowGrid(false);
    m_suppliers->setColumnCount(1);
    m_suppliers->setHorizontalHeaderLabels({QStringLiteral("المورد")});
    m_suppliers->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_suppliers->setSelectionMode(QAbstractItemView::SingleSelection);
    m_suppliers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_suppliers->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_suppliers->verticalHeader()->setDefaultSectionSize(42);
    m_suppliers->setStyleSheet(QStringLiteral("QTableWidget { border: 1px solid #e2e8f0; border-radius: 18px; background: rgba(255,255,255,0.82); }"
                                             "QHeaderView::section { background: #f8fafc; border: none; padding: 12px 10px; font-weight: 800; color: #334155; }"
                                             "QTableWidget::item { padding: 8px 10px; }"));

    m_transactions = new QTableWidget;
    m_transactions->setAlternatingRowColors(true);
    m_transactions->setFrameShape(QFrame::NoFrame);
    m_transactions->setShowGrid(false);
    m_transactions->setColumnCount(3);
    m_transactions->setHorizontalHeaderLabels(
        {QStringLiteral("التاريخ"), QStringLiteral("المبلغ"), QStringLiteral("ملاحظة")});
    m_transactions->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_transactions->horizontalHeader()->setStretchLastSection(true);
    m_transactions->verticalHeader()->setDefaultSectionSize(42);
    m_transactions->setStyleSheet(QStringLiteral("QTableWidget { border: 1px solid #e2e8f0; border-radius: 18px; background: rgba(255,255,255,0.82); }"
                                                "QHeaderView::section { background: #f8fafc; border: none; padding: 12px 10px; font-weight: 800; color: #334155; }"
                                                "QTableWidget::item { padding: 8px 10px; }"));

    auto* addRow = new QHBoxLayout;
    addRow->setSpacing(10);
    addRow->addStretch(1);
    addRow->addWidget(add);
    addRow->addWidget(edit);
    addRow->addWidget(m_addInvoice);

    auto* suppliersCard = makeCard();
    auto* suppliersLayout = new QVBoxLayout(suppliersCard);
    suppliersLayout->setContentsMargins(18, 16, 18, 16);
    suppliersLayout->setSpacing(10);
    suppliersLayout->addWidget(makeCardTitle(QStringLiteral("الموردون")));
    suppliersLayout->addLayout(addRow);
    suppliersLayout->addWidget(m_suppliers, 1);

    auto* transactionsCard = makeCard();
    auto* transactionsLayout = new QVBoxLayout(transactionsCard);
    transactionsLayout->setContentsMargins(18, 16, 18, 16);
    transactionsLayout->setSpacing(10);
    transactionsLayout->addWidget(makeCardTitle(QStringLiteral("فواتير المورد المحدد")));
    transactionsLayout->addWidget(m_transactions, 1);

    auto* split = new QHBoxLayout;
    split->setSpacing(12);
    split->addWidget(suppliersCard, 2);
    split->addWidget(transactionsCard, 3);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(QStringLiteral("الموردون"),
                                   QStringLiteral("الموردون والحسابات الآجلة عندهم")));
    root->addLayout(split, 1);

    connect(add, &QPushButton::clicked, this, &SuppliersPage::onAddClicked);
    connect(edit, &QPushButton::clicked, this, &SuppliersPage::onEditClicked);
    connect(m_addInvoice, &QPushButton::clicked, this, &SuppliersPage::onAddTransactionClicked);
    connect(m_suppliers, &QTableWidget::itemSelectionChanged, this, &SuppliersPage::onSelectionChanged);

    refresh();
}

void SuppliersPage::refresh()
{
    data::SupplierRepository suppliers(m_db);
    const auto all = suppliers.findAll();

    m_suppliers->setRowCount(0);
    for (const core::Supplier& supplier : all) {
        const int row = m_suppliers->rowCount();
        m_suppliers->insertRow(row);
        m_suppliers->setItem(row, 0, new QTableWidgetItem(supplier.name));
        m_suppliers->item(row, 0)->setData(Qt::UserRole, supplier.id);
    }
    onSelectionChanged();
}

int SuppliersPage::supplierCount() const
{
    return m_suppliers->rowCount();
}

int SuppliersPage::transactionCount() const
{
    return m_transactions->rowCount();
}

int SuppliersPage::selectedSupplierId() const
{
    const int row = m_suppliers->currentRow();
    if (row < 0) {
        return 0;
    }
    const QTableWidgetItem* item = m_suppliers->item(row, 0);
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

void SuppliersPage::reloadTransactions()
{
    m_transactions->setRowCount(0);
    const int supplierId = selectedSupplierId();
    if (supplierId == 0) {
        return;
    }
    data::SupplierTransactionRepository transactions(m_db);
    for (const core::SupplierTransaction& tx : transactions.findBySupplierId(supplierId)) {
        const int row = m_transactions->rowCount();
        m_transactions->insertRow(row);
        m_transactions->setItem(
            row, 0,
            new QTableWidgetItem(tx.createdAt.isValid()
                                     ? tx.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                     : QString()));
        m_transactions->setItem(row, 1, new QTableWidgetItem(formatMoney(tx.amountCents)));
        m_transactions->setItem(row, 2, new QTableWidgetItem(tx.note));
    }
}

void SuppliersPage::onSelectionChanged()
{
    const bool has = selectedSupplierId() != 0;
    m_addInvoice->setEnabled(has);
    reloadTransactions();
}

void SuppliersPage::onAddClicked()
{
    const auto maybeSupplier = supplierDialog(this, true, core::Supplier{});
    if (!maybeSupplier) {
        return;
    }
    data::SupplierRepository suppliers(m_db);
    if (suppliers.save(*maybeSupplier) == 0) {
        QMessageBox::warning(this, QStringLiteral("خطأ"), QStringLiteral("تعذر حفظ المورد"));
        return;
    }
    refresh();
}

void SuppliersPage::onEditClicked()
{
    const int id = selectedSupplierId();
    if (id == 0) {
        return;
    }
    data::SupplierRepository suppliers(m_db);
    const auto existing = suppliers.findById(id);
    if (!existing) {
        return;
    }
    const auto maybeSupplier = supplierDialog(this, false, *existing);
    if (!maybeSupplier) {
        return;
    }
    suppliers.save(*maybeSupplier);
    refresh();
}

void SuppliersPage::onAddTransactionClicked()
{
    const int id = selectedSupplierId();
    if (id == 0) {
        return;
    }
    data::SupplierRepository suppliers(m_db);
    const auto existing = suppliers.findById(id);
    if (!existing) {
        return;
    }
    const auto maybeInvoice = invoiceDialog(this, existing->name);
    if (!maybeInvoice) {
        return;
    }
    core::SupplierTransaction transaction;
    transaction.supplierId = id;
    transaction.amountCents = maybeInvoice->amountCents;
    transaction.note = maybeInvoice->note;
    transaction.createdAt = QDateTime::currentDateTime();

    data::SupplierTransactionRepository repo(m_db);
    if (repo.insert(transaction) == 0) {
        QMessageBox::warning(this, QStringLiteral("خطأ"), QStringLiteral("تعذر حفظ الفاتورة"));
        return;
    }
    reloadTransactions();
}

} // namespace app::ui