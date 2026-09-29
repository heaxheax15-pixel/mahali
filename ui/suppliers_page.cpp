#include "suppliers_page.h"

#include <QIcon>
#include <QAbstractButton>
#include <QCoreApplication>
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

#include "scan_safe_dialog.h"
#include "data/supplier_repository.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

std::optional<core::Supplier> supplierDialog(QWidget* parent, bool forNew, const core::Supplier& initial)
{
    ScanSafeDialog dialog(parent);
    dialog.setWindowTitle(forNew ? QCoreApplication::translate("app::ui::SuppliersPage", "مورد جديد")
                                 : QCoreApplication::translate("app::ui::SuppliersPage", "تعديل المورد"));
    dialog.setModal(true);

    auto* name = new QLineEdit(initial.name);

    QFormLayout* form = new QFormLayout;
    form->addRow(QCoreApplication::translate("app::ui::SuppliersPage", "الاسم"), name);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton* okBtn = buttons->button(QDialogButtonBox::Ok);
    QPushButton* cancelBtn = buttons->button(QDialogButtonBox::Cancel);
    okBtn->setText(QStringLiteral("OK"));
    cancelBtn->setText(QStringLiteral("Annuler"));
    okBtn->setIcon(QIcon());
    cancelBtn->setIcon(QIcon());
    for (QAbstractButton* b : buttons->buttons()) {
        if (auto* pb = qobject_cast<QPushButton*>(b)) {
            pb->setAutoDefault(false);
            pb->setDefault(false);
        }
    }
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

} // namespace

SuppliersPage::SuppliersPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* add = new QPushButton(tr("إضافة مورد"));
    add->setObjectName(QStringLiteral("primary"));
    add->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    auto* edit = new QPushButton(tr("تعديل"));
    edit->setObjectName(QStringLiteral("secondary"));

    m_suppliers = new QTableWidget;
    m_suppliers->setObjectName(QStringLiteral("supplierTable"));
    m_suppliers->setAlternatingRowColors(true);
    m_suppliers->setFrameShape(QFrame::NoFrame);
    m_suppliers->setShowGrid(false);
    m_suppliers->setColumnCount(1);
    m_suppliers->setHorizontalHeaderLabels({tr("المورد")});
    m_suppliers->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_suppliers->setSelectionMode(QAbstractItemView::SingleSelection);
    m_suppliers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_suppliers->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_suppliers->verticalHeader()->setDefaultSectionSize(42);

    auto* addRow = new QHBoxLayout;
    addRow->setSpacing(10);
    addRow->addStretch(1);
    addRow->addWidget(add);
    addRow->addWidget(edit);

    auto* suppliersCard = makeCard();
    auto* suppliersLayout = new QVBoxLayout(suppliersCard);
    suppliersLayout->setContentsMargins(18, 16, 18, 16);
    suppliersLayout->setSpacing(10);
    suppliersLayout->addWidget(makeCardTitle(tr("الموردون")));
    suppliersLayout->addLayout(addRow);
    suppliersLayout->addWidget(m_suppliers, 1);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(tr("الموردون"),
                                   tr("الموردون والحسابات الآجلة عندهم")));
    root->addWidget(suppliersCard, 1);

    connect(add, &QPushButton::clicked, this, &SuppliersPage::onAddClicked);
    connect(edit, &QPushButton::clicked, this, &SuppliersPage::onEditClicked);

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
}

int SuppliersPage::supplierCount() const
{
    return m_suppliers->rowCount();
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

void SuppliersPage::onAddClicked()
{
    const auto maybeSupplier = supplierDialog(this, true, core::Supplier{});
    if (!maybeSupplier) {
        return;
    }
    data::SupplierRepository suppliers(m_db);
    if (suppliers.save(*maybeSupplier) == 0) {
        QMessageBox::warning(this, tr("خطأ"), tr("تعذر حفظ المورد"));
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

} // namespace app::ui