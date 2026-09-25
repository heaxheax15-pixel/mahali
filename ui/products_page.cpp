#include "products_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "data/product_repository.h"
#include "data/stock_movement_repository.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"
#include "format_utils.h"

namespace app::ui {

namespace {

struct StockAdjustment {
    long long delta = 0;
    QString reason;
};

std::optional<core::Product> productDialog(QWidget* parent, bool forNew, const core::Product& initial)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(forNew ? QStringLiteral("منتج جديد") : QStringLiteral("تعديل المنتج"));
    dialog.setModal(true);

    auto* barcode = new QLineEdit(initial.barcode);
    auto* name = new QLineEdit(initial.name);
    auto* cost = new QLineEdit(forNew ? QString() : formatMoney(initial.costPriceCents));
    auto* sale = new QLineEdit(forNew ? QString() : formatMoney(initial.salePriceCents));
    auto* unit = new QLineEdit(initial.unit);
    auto* package = new QSpinBox;
    package->setRange(1, 1000000);
    package->setValue(initial.packageSize ? initial.packageSize : 1);
    auto* active = new QCheckBox;
    active->setChecked(forNew || initial.active);

    QFormLayout* form = new QFormLayout;
    form->addRow(QStringLiteral("الباركود"), barcode);
    form->addRow(QStringLiteral("الاسم"), name);
    form->addRow(QStringLiteral("سعر التكلفة"), cost);
    form->addRow(QStringLiteral("سعر البيع"), sale);
    form->addRow(QStringLiteral("الوحدة"), unit);
    form->addRow(QStringLiteral("المحتوى (عدد وحدات الوجبة)"), package);
    form->addRow(QStringLiteral("مُفعّل"), active);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    const auto costCents = parseMoney(cost->text());
    const auto saleCents = parseMoney(sale->text());
    if (!costCents || !saleCents) {
        return std::nullopt;
    }
    if (barcode->text().trimmed().isEmpty() || name->text().trimmed().isEmpty()) {
        return std::nullopt;
    }

    core::Product product = initial;
    product.barcode = barcode->text().trimmed();
    product.name = name->text().trimmed();
    product.costPriceCents = *costCents;
    product.salePriceCents = *saleCents;
    product.unit = unit->text().trimmed();
    product.packageSize = package->value();
    product.active = active->isChecked();
    return product;
}

std::optional<StockAdjustment> stockDialog(QWidget* parent, const QString& productName)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("تعديل المخزون — %1").arg(productName));
    dialog.setModal(true);

    auto* delta = new QSpinBox;
    delta->setRange(-100000000, 100000000);
    delta->setValue(0);
    auto* reason = new QComboBox;
    reason->addItem(QStringLiteral("توريد جديد"), QStringLiteral("purchase"));
    reason->addItem(QStringLiteral("تعديل يدوي"), QStringLiteral("manual_adjustment"));
    reason->addItem(QStringLiteral("تالف / منتهي"), QStringLiteral("spoilage"));

    QFormLayout* form = new QFormLayout;
    form->addRow(QStringLiteral("الكمية (+توريد / -خسارة)"), delta);
    form->addRow(QStringLiteral("السبب"), reason);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    StockAdjustment adjustment;
    adjustment.delta = delta->value();
    adjustment.reason = reason->currentData().toString();
    return adjustment;
}

} // namespace

ProductsPage::ProductsPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("searchField"));
    m_search->setPlaceholderText(QStringLiteral("بحث بالباركود أو الاسم..."));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(appIcon(Icon::Search, QColor(QStringLiteral("#66757a")), 18),
                        QLineEdit::LeadingPosition);
    m_add = new QPushButton(QStringLiteral("إضافة منتج"));
    m_add->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    m_edit = new QPushButton(QStringLiteral("تعديل"));
    m_edit->setObjectName(QStringLiteral("secondary"));
    m_stock = new QPushButton(QStringLiteral("تعديل المخزون"));
    m_stock->setObjectName(QStringLiteral("secondary"));

    m_table = new QTableWidget;
    m_table->setAlternatingRowColors(true);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({QStringLiteral("الباركود"), QStringLiteral("الاسم"),
                                        QStringLiteral("سعر التكلفة"), QStringLiteral("سعر البيع"),
                                        QStringLiteral("الكمية"), QStringLiteral("الوحدة"),
                                        QStringLiteral("الحالة")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(m_search, 1);
    toolbar->addWidget(m_add);
    toolbar->addWidget(m_edit);
    toolbar->addWidget(m_stock);

    auto* card = makeCard();
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 14, 14);
    cardLayout->setSpacing(10);
    cardLayout->addWidget(makeCardTitle(QStringLiteral("المنتجات")));
    cardLayout->addLayout(toolbar);
    cardLayout->addWidget(m_table, 1);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(QStringLiteral("المنتجات"),
                                   QStringLiteral("إدارة الأصناف، الأسعار والمخزون")));
    root->addWidget(card, 1);

    connect(m_search, &QLineEdit::textChanged, this, &ProductsPage::onSearchChanged);
    connect(m_add, &QPushButton::clicked, this, &ProductsPage::onAddClicked);
    connect(m_edit, &QPushButton::clicked, this, &ProductsPage::onEditClicked);
    connect(m_stock, &QPushButton::clicked, this, &ProductsPage::onStockClicked);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ProductsPage::onSelectionChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &ProductsPage::onEditClicked);

    m_edit->setEnabled(false);
    m_stock->setEnabled(false);
    refresh();
}

void ProductsPage::refresh()
{
    const QString filter = m_search->text().trimmed();
    data::ProductRepository products(m_db);
    const auto all = products.findAll();

    m_table->setRowCount(0);
    for (const core::Product& product : all) {
        if (!filter.isEmpty() && !product.barcode.contains(filter, Qt::CaseInsensitive)
            && !product.name.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(product.barcode));
        m_table->setItem(row, 1, new QTableWidgetItem(product.name));
        m_table->setItem(row, 2, new QTableWidgetItem(formatMoney(product.costPriceCents)));
        m_table->setItem(row, 3, new QTableWidgetItem(formatMoney(product.salePriceCents)));
        m_table->setItem(row, 4, new QTableWidgetItem(QString::number(product.quantity)));
        m_table->setItem(row, 5, new QTableWidgetItem(product.unit));
        m_table->setItem(row, 6, new QTableWidgetItem(product.active ? QStringLiteral("مُفعل")
                                                                     : QStringLiteral("موقوف")));
        m_table->item(row, 0)->setData(Qt::UserRole, product.id);
    }
    onSelectionChanged();
}

int ProductsPage::rowCount() const
{
    return m_table->rowCount();
}

int ProductsPage::selectedProductId() const
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return 0;
    }
    const QTableWidgetItem* item = m_table->item(row, 0);
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

void ProductsPage::onSearchChanged(const QString&)
{
    refresh();
}

void ProductsPage::onSelectionChanged()
{
    const bool has = selectedProductId() != 0;
    m_edit->setEnabled(has);
    m_stock->setEnabled(has);
}

void ProductsPage::onAddClicked()
{
    const auto maybeProduct = productDialog(this, true, core::Product{});
    if (!maybeProduct) {
        return;
    }
    data::ProductRepository products(m_db);
    const int id = products.save(*maybeProduct);
    if (id == 0) {
        QMessageBox::warning(this, QStringLiteral("خطأ"),
                             QStringLiteral("تعذر حفظ المنتج — الباركود مستخدم مسبقاً أو بيانات ناقصة"));
        return;
    }
    refresh();
}

void ProductsPage::onEditClicked()
{
    const int id = selectedProductId();
    if (id == 0) {
        return;
    }
    data::ProductRepository products(m_db);
    const auto existing = products.findById(id);
    if (!existing) {
        return;
    }
    const auto maybeProduct = productDialog(this, false, *existing);
    if (!maybeProduct) {
        return;
    }
    const int saved = products.save(*maybeProduct);
    if (saved == 0) {
        QMessageBox::warning(this, QStringLiteral("خطأ"),
                             QStringLiteral("تعذر حفظ التعديل — الباركود مستخدم مسبقاً"));
        return;
    }
    refresh();
}

void ProductsPage::onStockClicked()
{
    const int id = selectedProductId();
    if (id == 0) {
        return;
    }
    data::ProductRepository products(m_db);
    const auto existing = products.findById(id);
    if (!existing) {
        return;
    }
    const auto maybeAdj = stockDialog(this, existing->name);
    if (!maybeAdj || maybeAdj->delta == 0) {
        return;
    }
    if (maybeAdj->delta < 0 && existing->quantity + maybeAdj->delta < 0) {
        QMessageBox::warning(this, QStringLiteral("خطأ"),
                             QStringLiteral("الكمية السالبة أكبر من المتوفر (الكثافة %1)")
                                 .arg(existing->quantity));
        return;
    }
    products.adjustStock(id, maybeAdj->delta, maybeAdj->reason);
    refresh();
}

} // namespace app::ui