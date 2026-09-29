#include "products_page.h"

#include <QAbstractButton>
#include <QColor>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "core/product.h"
#include "data/product_repository.h"
#include "data/stock_movement_repository.h"
#include "dialogs/product_dialog.h"
#include "format_utils.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

// Column order shared by the grid, the footer counters and the edit handlers.
constexpr int kColBarcode = 0;
constexpr int kColName = 1;
constexpr int kColCost = 2;
constexpr int kColSale = 3;
constexpr int kColQuantity = 4;
constexpr int kColUnit = 5;
constexpr int kColState = 6;

// "Stock bas" is the reorder band: on the shelf but nearly gone. It deliberately
// excludes zero and anything below it, which the negative chip owns.
constexpr long long kLowStockCeiling = 10;
constexpr int kSearchDebounceMs = 200;

} // namespace

ProductsPage::ProductsPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* root = new QVBoxLayout(this);
    padPageLayout(root);

    root->addWidget(new PageHeader(tr("Produits"), tr("Gérez les articles, les prix et le stock")));

    // ---- card 1: search, filters, add ----
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("searchField"));
    m_search->setMinimumHeight(48);
    m_search->setPlaceholderText(tr("Rechercher un nom, un code-barres..."));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(appIcon(Icon::Search, QColor(QStringLiteral("#66757a")), 18),
                        QLineEdit::LeadingPosition);

    // Typing refreshes straight away, but one keystroke at a time against a
    // full catalogue is a lot of queries for nothing: the grid follows 200ms
    // after the last key instead.
    m_searchDebounce = new QTimer(this);
    m_searchDebounce->setSingleShot(true);
    m_searchDebounce->setInterval(kSearchDebounceMs);

    const QList<QPair<QString, QString>> filters = {
        {QStringLiteral("all"), tr("Tous")},
        {QStringLiteral("active"), tr("Actifs")},
        {QStringLiteral("low"), tr("Stock bas")},
        {QStringLiteral("negative"), tr("Stock négatif")},
    };
    auto* chipRow = new QHBoxLayout;
    chipRow->setSpacing(8);
    for (const auto& [key, label] : filters) {
        auto* chip = new QPushButton(label);
        chip->setObjectName(QStringLiteral("filterChip"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setCheckable(true);
        chip->setProperty("filterKey", key);
        m_chips.insert(key, chip);
        chipRow->addWidget(chip);
    }
    chipRow->addStretch(1);

    m_add = new QPushButton(tr("Ajouter"));
    m_add->setObjectName(QStringLiteral("primary"));
    m_add->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));

    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(10);
    toolbar->addWidget(m_search, 1);
    toolbar->addLayout(chipRow);
    toolbar->addWidget(m_add);

    auto* toolbarCard = makeCard();
    auto* toolbarLayout = new QVBoxLayout(toolbarCard);
    toolbarLayout->setContentsMargins(18, 16, 18, 16);
    toolbarLayout->setSpacing(12);
    toolbarLayout->addLayout(toolbar);

    // ---- card 2: the grid ----
    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("productTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(false);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({tr("Code-barres"), tr("Nom"), tr("Prix rev."),
                                        tr("Prix vente"), tr("Qté"), tr("Unité"), tr("État")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setDefaultSectionSize(42);

    auto* gridCard = makeCard();
    auto* gridLayout = new QVBoxLayout(gridCard);
    gridLayout->setContentsMargins(18, 16, 18, 16);
    gridLayout->setSpacing(12);
    gridLayout->addWidget(makeCardTitle(tr("Catalogue")));
    gridLayout->addWidget(m_table, 1);

    // ---- footer ----
    m_footer = new QLabel;
    m_footer->setObjectName(QStringLiteral("faintText"));

    root->addWidget(toolbarCard);
    root->addWidget(gridCard, 1);
    root->addWidget(m_footer);

    connect(m_search, &QLineEdit::textChanged, this, &ProductsPage::onSearchChanged);
    connect(m_searchDebounce, &QTimer::timeout, this, &ProductsPage::refresh);
    connect(m_add, &QPushButton::clicked, this, &ProductsPage::onAddClicked);
    connect(m_table, &QTableWidget::itemChanged, this, &ProductsPage::onItemChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &ProductsPage::onNameActivated);
    for (auto* chip : m_chips) {
        connect(chip, &QPushButton::clicked, this, &ProductsPage::onFilterChipClicked);
    }

    setFilterActive(m_filterKey);
    refresh();
}

void ProductsPage::refresh()
{
    const QString query = m_search->text().trimmed();
    const std::vector<core::Product> all = data::ProductRepository(m_db).findAll();

    int shown = 0;
    int active = 0;
    int low = 0;

    // The grid is rebuilt from scratch, so the edits made while filling it would
    // otherwise come straight back through itemChanged and write to the database.
    const QSignalBlocker blocker(m_table);
    m_updating = true;
    m_table->setRowCount(0);

    for (const core::Product& product : all) {
        if (!query.isEmpty() && !product.name.contains(query, Qt::CaseInsensitive)
            && !product.barcode.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        if (m_filterKey == QLatin1String("active") && !product.active) {
            continue;
        }
        if (m_filterKey == QLatin1String("low")
            && (product.quantity <= 0 || product.quantity > kLowStockCeiling)) {
            continue;
        }
        if (m_filterKey == QLatin1String("negative") && product.quantity >= 0) {
            continue;
        }

        ++shown;
        if (product.active) {
            ++active;
        }
        if (product.quantity > 0 && product.quantity <= kLowStockCeiling) {
            ++low;
        }

        const int row = m_table->rowCount();
        m_table->insertRow(row);

        const auto put = [this, row](int column, const QString& text, bool editable) {
            auto* cell = new QTableWidgetItem(text);
            // Everything but the prices and the count is a label, not a field.
            cell->setFlags(editable ? (cell->flags() | Qt::ItemIsEditable)
                                    : (cell->flags() & ~Qt::ItemIsEditable));
            cell->setData(Qt::UserRole, 0);
            m_table->setItem(row, column, cell);
        };
        put(kColBarcode, product.barcode, false);
        put(kColName, product.name, false);
        put(kColCost, formatMoney(product.costPriceCents), true);
        put(kColSale, formatMoney(product.salePriceCents), true);
        put(kColQuantity, QString::number(product.quantity), true);
        put(kColUnit, product.unit, false);
        put(kColState, product.active ? tr("Actif") : tr("Inactif"), false);

        for (int column = 0; column < m_table->columnCount(); ++column) {
            m_table->item(row, column)->setData(Qt::UserRole, product.id);
        }
    }

    m_updating = false;
    m_footer->setText(tr("Total : %1 produits · %2 actifs · %3 stock bas")
                          .arg(shown)
                          .arg(active)
                          .arg(low));
}

int ProductsPage::rowCount() const
{
    return m_table->rowCount();
}

int ProductsPage::productIdAt(int row) const
{
    if (row < 0 || row >= m_table->rowCount()) {
        return 0;
    }
    const QTableWidgetItem* cell = m_table->item(row, kColBarcode);
    return cell ? cell->data(Qt::UserRole).toInt() : 0;
}

void ProductsPage::setFilterActive(const QString& key)
{
    m_filterKey = key;
    for (auto it = m_chips.cbegin(); it != m_chips.cend(); ++it) {
        QPushButton* chip = it.value();
        const bool on = it.key() == key;
        // A stylesheet keyed on the object name needs a repolish to notice the
        // change; setObjectName alone repaints with the old rule still cached.
        chip->setObjectName(on ? QStringLiteral("filterChipActive") : QStringLiteral("filterChip"));
        chip->setChecked(on);
        chip->style()->unpolish(chip);
        chip->style()->polish(chip);
        chip->update();
    }
}

void ProductsPage::onSearchChanged()
{
    m_searchDebounce->start();
}

void ProductsPage::onFilterChipClicked()
{
    auto* chip = qobject_cast<QPushButton*>(sender());
    if (!chip) {
        return;
    }
    const QString key = chip->property("filterKey").toString();
    if (key.isEmpty()) {
        return;
    }
    setFilterActive(key);
    refresh();
}

void ProductsPage::onAddClicked()
{
    const std::optional<core::Product> maybeProduct = showProductDialog(this, m_db);
    if (!maybeProduct) {
        return;
    }
    if (data::ProductRepository(m_db).save(*maybeProduct) == 0) {
        QMessageBox::warning(this, tr("Erreur"),
                             tr("Impossible d'enregistrer le produit — code-barres déjà utilisé "
                                "ou données incomplètes"));
        return;
    }
    refresh();
}

void ProductsPage::onNameActivated(int row, int column)
{
    // Only the name opens the form. The price and quantity cells are editable,
    // and a double click there belongs to the cell editor, not to this.
    if (column != kColName) {
        return;
    }
    const int id = productIdAt(row);
    if (id == 0) {
        return;
    }
    const std::optional<core::Product> existing = data::ProductRepository(m_db).findById(id);
    if (!existing) {
        return;
    }
    const std::optional<core::Product> maybeProduct = showProductDialog(this, m_db, *existing);
    if (!maybeProduct) {
        return;
    }
    if (data::ProductRepository(m_db).save(*maybeProduct) == 0) {
        QMessageBox::warning(this, tr("Erreur"),
                             tr("Impossible d'enregistrer la modification — code-barres déjà utilisé"));
        return;
    }
    refresh();
}

bool ProductsPage::saveEditedPrice(int productId, int column, const QString& text)
{
    const std::optional<long long> cents = parseMoney(text);
    if (!cents || *cents < 0) {
        return false;
    }
    const std::optional<core::Product> product = data::ProductRepository(m_db).findById(productId);
    if (!product) {
        return false;
    }
    core::Product edited = *product;
    if (column == kColCost) {
        edited.costPriceCents = *cents;
    } else {
        edited.salePriceCents = *cents;
    }
    // save() updates the row in place and leaves quantity alone, so editing a
    // price cannot quietly restock the article.
    return data::ProductRepository(m_db).save(edited) != 0;
}

bool ProductsPage::applyQuantityEdit(int productId, long long oldQuantity, const QString& text)
{
    bool ok = false;
    const long long quantity = text.trimmed().toLongLong(&ok);
    if (!ok) {
        return false;
    }
    const long long delta = quantity - oldQuantity;
    if (delta == 0) {
        return true;
    }
    core::StockMovement movement;
    movement.productId = productId;
    movement.delta = delta;
    movement.reason = QStringLiteral("manual_adjustment");
    movement.reference = QStringLiteral("Products page");
    movement.createdAt = QDateTime::currentDateTime();
    // The trg_stock_after_insert trigger moves products.quantity by the delta,
    // so the stored count and the movement trail stay in step.
    return data::StockMovementRepository(m_db).insert(movement) != 0;
}

void ProductsPage::restoreCell(int row, int column)
{
    const int id = productIdAt(row);
    if (id == 0 || row >= m_table->rowCount()) {
        return;
    }
    const std::optional<core::Product> product = data::ProductRepository(m_db).findById(id);
    if (!product) {
        return;
    }
    QString canonical;
    if (column == kColCost) {
        canonical = formatMoney(product->costPriceCents);
    } else if (column == kColSale) {
        canonical = formatMoney(product->salePriceCents);
    } else if (column == kColQuantity) {
        canonical = QString::number(product->quantity);
    } else {
        return;
    }
    // Writing the old value fires itemChanged again; the blocker keeps that
    // second pass from being read as another edit.
    const QSignalBlocker blocker(m_table);
    m_table->item(row, column)->setText(canonical);
}

void ProductsPage::onItemChanged(QTableWidgetItem* item)
{
    if (m_updating || !item) {
        return;
    }
    const int row = item->row();
    const int column = item->column();
    if (column < kColCost || column > kColQuantity) {
        return;
    }
    const int productId = productIdAt(row);
    if (productId == 0) {
        return;
    }

    if (column == kColQuantity) {
        const std::optional<core::Product> product = data::ProductRepository(m_db).findById(productId);
        if (!product) {
            return;
        }
        if (applyQuantityEdit(productId, product->quantity, item->text())) {
            return;
        }
        restoreCell(row, column);
        QMessageBox::warning(this, tr("Erreur"), tr("Quantité invalide — l'ancien stock a été restauré."));
        return;
    }

    if (saveEditedPrice(productId, column, item->text())) {
        return;
    }
    restoreCell(row, column);
    QMessageBox::warning(this, tr("Erreur"), tr("Prix invalide — l'ancienne valeur a été restaurée."));
}

} // namespace app::ui
