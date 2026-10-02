#include "pos_page.h"

#include <QBrush>
#include <QAbstractTableModel>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableView>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QStyle>

#include <algorithm>
#include <functional>

#include "core/barcode_utils.h"
#include "core/session.h"
#include "data/audit_log_repository.h"
#include "data/cash_session_repository.h"
#include "data/product_repository.h"
#include "data/sale_service.h"
#include "dialogs/product_dialog.h"
#include "format_utils.h"
#include "quick_items_bar.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "theme_tokens.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

class PosProductModel final : public QAbstractTableModel {
public:
    enum Role {
        ProductIdRole = Qt::UserRole + 1,
        UnitRole,
    };

    explicit PosProductModel(data::Database& db, QObject* parent = nullptr)
        : QAbstractTableModel(parent)
        , m_db(db)
    {
        refresh();
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_products.size());
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 5;
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) {
            return {};
        }
        const core::Product& product = m_products[static_cast<std::size_t>(index.row())];
        if (role == ProductIdRole) {
            return product.id;
        }
        if (role == UnitRole) {
            return product.unit;
        }
        if (role != Qt::DisplayRole) {
            return {};
        }
        switch (index.column()) {
        case 0: return product.name;
        case 1: return product.barcode;
        case 2: return product.unit;
        case 3: return QString::number(product.quantity);
        case 4: return formatMoney(product.salePriceCents);
        default: return {};
        }
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
            return {};
        }
        switch (section) {
        case 0: return tr("Produit");
        case 1: return tr("Code-barres");
        case 2: return tr("Unité");
        case 3: return tr("Stock");
        case 4: return tr("Prix");
        default: return {};
        }
    }

    void refresh()
    {
        beginResetModel();
        m_products = data::ProductRepository(m_db).findAll();
        m_products.erase(std::remove_if(m_products.begin(), m_products.end(), [](const core::Product& product) {
            return !product.active || product.barcode.trimmed().isEmpty();
        }), m_products.end());
        endResetModel();
    }

    QStringList units() const
    {
        QStringList result;
        for (const core::Product& product : m_products) {
            if (!product.unit.trimmed().isEmpty() && !result.contains(product.unit)) {
                result.append(product.unit);
            }
        }
        result.sort(Qt::CaseInsensitive);
        return result;
    }

    std::optional<core::Product> productById(int id) const
    {
        const auto found = std::find_if(m_products.cbegin(), m_products.cend(),
                                        [id](const core::Product& product) { return product.id == id; });
        return found == m_products.cend() ? std::nullopt : std::optional<core::Product>(*found);
    }

private:
    data::Database& m_db;
    std::vector<core::Product> m_products;
};

class PosProductFilter final : public QSortFilterProxyModel {
public:
    explicit PosProductFilter(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setFilterKeyColumn(-1);
    }

    void setUnitFilter(const QString& unit)
    {
        if (m_unit == unit) {
            return;
        }
        m_unit = unit;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        if (!QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent)) {
            return false;
        }
        if (m_unit.isEmpty()) {
            return true;
        }
        const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        return sourceModel()->data(index, PosProductModel::UnitRole).toString() == m_unit;
    }

private:
    QString m_unit;
};

PosPage::PosPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* root = new QVBoxLayout(this);
    padPageLayout(root);

    // No subtitle: the scan field directly below repeats it as its placeholder,
    // and a page header is a fixed block either way, so the line was costing
    // about 20px of grid for a sentence the operator reads twice. PageHeader
    // hides an empty subtitle itself, so nothing is left of it.
    auto* header = new PageHeader(tr("Vente rapide"), QString());
    m_sessionChip = makeChip(tr("Session"), QStringLiteral("info"));
    header->addAction(m_sessionChip);
    root->addWidget(header);

    // The barcode field is the only thing that has to be reachable without
    // touching the mouse, so it stays the tallest control on the page even at
    // 49px: a scan target smaller than the buttons around it stops reading as
    // the primary input. The inline rule overrides the app-wide #searchField
    // min-height, and the padding comes down with the height so the text keeps
    // its 18px and the box loses only its slack.
    m_entry = new QLineEdit;
    m_entry->setObjectName(QStringLiteral("posBarcodeField"));
    m_entry->setPlaceholderText(tr("Code-barres ou nom du produit"));
    m_entry->setClearButtonEnabled(true);
    m_entry->setFixedHeight(44);
    m_entry->addAction(appIcon(Icon::Search, QColor(QStringLiteral("#66757a")), 18),
                       QLineEdit::LeadingPosition);
    root->addWidget(m_entry);

    auto* workspace = new QSplitter(Qt::Horizontal);
    workspace->setObjectName(QStringLiteral("posWorkspace"));
    workspace->setChildrenCollapsible(false);
    workspace->setHandleWidth(themeTokens::space4);

    auto* catalog = new QWidget;
    auto* catalogLayout = new QVBoxLayout(catalog);
    catalogLayout->setContentsMargins(0, 0, 0, 0);
    catalogLayout->setSpacing(themeTokens::space8);

    m_productModel = new PosProductModel(m_db, this);
    m_productFilter = new PosProductFilter(this);
    m_productFilter->setSourceModel(m_productModel);

    auto* catalogControls = new QWidget;
    auto* catalogControlsLayout = new QHBoxLayout(catalogControls);
    catalogControlsLayout->setContentsMargins(0, 0, 0, 0);
    catalogControlsLayout->setSpacing(themeTokens::space8);
    auto* catalogSearch = new QLineEdit;
    catalogSearch->setObjectName(QStringLiteral("posCatalogSearch"));
    catalogSearch->setPlaceholderText(tr("Rechercher dans les produits"));
    catalogSearch->setClearButtonEnabled(true);
    catalogSearch->setFixedHeight(40);
    m_unitFilter = new QComboBox;
    m_unitFilter->setObjectName(QStringLiteral("posUnitFilter"));
    m_unitFilter->addItem(tr("Toutes les unités"), QString());
    for (const QString& unit : m_productModel->units()) {
        m_unitFilter->addItem(unit, unit);
    }
    catalogControlsLayout->addWidget(catalogSearch, 1);
    catalogControlsLayout->addWidget(m_unitFilter);
    catalogLayout->addWidget(catalogControls);

    m_productTable = new QTableView;
    m_productTable->setObjectName(QStringLiteral("posProductTable"));
    m_productTable->setModel(m_productFilter);
    m_productTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_productTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_productTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_productTable->setAlternatingRowColors(true);
    m_productTable->verticalHeader()->hide();
    m_productTable->verticalHeader()->setDefaultSectionSize(38);
    m_productTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_productTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_productTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_productTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_productTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_productTable->setColumnWidth(1, 88);
    m_productTable->setColumnWidth(2, 56);
    m_productTable->setColumnWidth(3, 48);
    m_productTable->setColumnWidth(4, 72);
    catalogLayout->addWidget(m_productTable, 1);

    m_quickItems = new QuickItemsBar(m_db, this);
    m_quickItems->setFixedHeight(48);
    catalogLayout->addWidget(m_quickItems);

    auto* cart = new QWidget;
    auto* cartLayout = new QVBoxLayout(cart);
    cartLayout->setContentsMargins(0, 0, 0, 0);
    cartLayout->setSpacing(themeTokens::space8);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("posTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(true);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({tr("Produit"), tr("Qté"), tr("PU"), tr("Total")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Qty and PU are edited through QInputDialog on double click, which is the
    // only way to get a number pad on a tablet. Leaving the items editable as
    // well would let a stray keypress silently rewrite a price.
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(38);
    m_table->verticalHeader()->hide();
    m_table->setColumnWidth(1, 52);
    m_table->setColumnWidth(2, 72);
    m_table->setColumnWidth(3, 84);
    cartLayout->addWidget(m_table, 1);

    auto* clearButton = new QPushButton(tr("Vider"));
    clearButton->setObjectName(QStringLiteral("secondary"));
    clearButton->setIcon(appIcon(Icon::Trash, QColor(QStringLiteral("#475569")), 18));
    clearButton->setText(QString());
    clearButton->setToolTip(tr("Vider la facture"));
    auto* removeButton = new QPushButton(tr("Retirer ligne"));
    removeButton->setObjectName(QStringLiteral("secondary"));
    removeButton->setIcon(appIcon(Icon::X, QColor(QStringLiteral("#475569")), 18));
    removeButton->setText(QString());
    removeButton->setToolTip(tr("Retirer la ligne sélectionnée"));

    // ---- the invoice bar, under the grid ----
    // One horizontal band rather than the rail it replaces. The total is the
    // largest thing on the page, so it leads and the line count sits under it:
    // reading a sale means reading the money first, then how it was made up.
    auto* invoiceBar = new QFrame;
    invoiceBar->setObjectName(QStringLiteral("invoiceBar"));
    invoiceBar->setMinimumHeight(104);

    auto* totals = new QVBoxLayout;
    totals->setSpacing(0);
    totals->setContentsMargins(0, 0, 0, 0);

    auto* totalCaption = new QLabel(tr("Total"));
    totalCaption->setObjectName(QStringLiteral("heroCaption"));

    m_totalLabel = new QLabel(QStringLiteral("0.00"));
    m_totalLabel->setObjectName(QStringLiteral("heroValue"));

    m_countLabel = new QLabel;
    m_countLabel->setObjectName(QStringLiteral("heroCaption"));

    totals->addWidget(totalCaption);
    totals->addWidget(m_totalLabel);
    totals->addWidget(m_countLabel);

    auto* paymentSummary = new QVBoxLayout;
    paymentSummary->setContentsMargins(0, 0, 0, 0);
    paymentSummary->setSpacing(themeTokens::space4);
    m_paidLabel = new QLabel;
    m_remainingLabel = new QLabel;
    m_paidLabel->setObjectName(QStringLiteral("invoiceSecondary"));
    m_remainingLabel->setObjectName(QStringLiteral("invoiceSecondary"));
    paymentSummary->addWidget(m_paidLabel);
    paymentSummary->addWidget(m_remainingLabel);

    m_save = new QPushButton(tr("Enregistrer la vente"));
    m_save->setObjectName(QStringLiteral("primary"));
    m_save->setMinimumWidth(0);
    m_save->setFixedSize(44, 36);
    m_save->setIcon(appIcon(Icon::Check, QColor(QStringLiteral("#ffffff")), 20));
    m_save->setText(QString());
    m_save->setToolTip(tr("Enregistrer la vente"));

    auto* barLayout = new QVBoxLayout(invoiceBar);
    barLayout->setContentsMargins(themeTokens::space8, themeTokens::space8,
                                  themeTokens::space8, themeTokens::space8);
    barLayout->setSpacing(themeTokens::space4);
    auto* amountRow = new QHBoxLayout;
    amountRow->setContentsMargins(0, 0, 0, 0);
    amountRow->setSpacing(themeTokens::space8);
    amountRow->addLayout(totals, 1);
    amountRow->addLayout(paymentSummary, 1);
    barLayout->addLayout(amountRow, 1);

    auto* actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(themeTokens::space4);
    for (QPushButton* action : {m_save, removeButton, clearButton}) {
        action->setFixedSize(44, 36);
    }
    actionRow->addWidget(m_save);
    actionRow->addWidget(removeButton);
    actionRow->addWidget(clearButton);
    actionRow->addStretch(1);
    barLayout->addLayout(actionRow);

    cartLayout->addWidget(invoiceBar);

    workspace->addWidget(catalog);
    workspace->addWidget(cart);
    workspace->setStretchFactor(0, 65);
    workspace->setStretchFactor(1, 35);
    workspace->setSizes({650, 350});
    root->addWidget(workspace, 1);

    m_notice = new QLabel;
    m_notice->setWordWrap(true);
    m_notice->setObjectName(QStringLiteral("noticeOk"));
    // Nothing to report yet, so it starts hidden; setNotice reveals it.
    m_notice->setVisible(false);
    root->addWidget(m_notice);

    connect(m_entry, &QLineEdit::returnPressed, this, &PosPage::addEntry);
    connect(m_entry, &QLineEdit::textChanged, this, &PosPage::onBarcodeTextChanged);
    connect(m_save, &QPushButton::clicked, this, &PosPage::completeSale);
    connect(clearButton, &QPushButton::clicked, this, &PosPage::onClearCart);
    connect(removeButton, &QPushButton::clicked, this, &PosPage::onRemoveLine);
    connect(m_table, &QTableWidget::cellChanged, this, &PosPage::onCellChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &PosPage::onCellDoubleClicked);
    connect(m_quickItems, &QuickItemsBar::productClicked, this, &PosPage::onQuickItemClicked);
    connect(m_quickItems, &QuickItemsBar::addNewRequested, this, &PosPage::onAddQuickProduct);
    connect(catalogSearch, &QLineEdit::textChanged, m_productFilter,
            &QSortFilterProxyModel::setFilterFixedString);
    connect(m_unitFilter, &QComboBox::currentIndexChanged, this, [this](int index) {
        static_cast<PosProductFilter*>(m_productFilter)->setUnitFilter(m_unitFilter->itemData(index).toString());
    });
    const auto addSelectedProduct = [this](const QModelIndex& proxyIndex) {
        if (!proxyIndex.isValid()) {
            return;
        }
        const int productId = proxyIndex.data(PosProductModel::ProductIdRole).toInt();
        if (const auto product = m_productModel->productById(productId)) {
            addProductToCart(*product, 1);
        }
    };
    connect(m_productTable, &QTableView::activated, this, addSelectedProduct);

    // The filter is installed on the page itself as well as on the entry field,
    // because the entry is not the only thing it watches: the page's own Show
    // event is what tells us the register has become the visible page, and that
    // has to reach the entry for the cashier's next scan.
    m_entry->installEventFilter(this);
    installEventFilter(this);
    m_table->installEventFilter(this);

    refreshTotals();
    refreshSessionChip();
    m_quickItems->refresh();
    m_entry->setFocus();
}

void PosPage::onBarcodeTextChanged(const QString& text)
{
    const QString normalized = core::normalizeScannedBarcode(text);
    if (normalized == text) {
        return;
    }
    // setText emits textChanged again; without the blocker this recurses.
    QSignalBlocker blocker(m_entry);
    m_entry->setText(normalized);
    m_entry->setCursorPosition(normalized.length());
}

void PosPage::refreshCatalog()
{
    m_productModel->refresh();
    const QString selectedUnit = m_unitFilter->currentData().toString();
    const QSignalBlocker blocker(m_unitFilter);
    m_unitFilter->clear();
    m_unitFilter->addItem(tr("Toutes les unités"), QString());
    for (const QString& unit : m_productModel->units()) {
        m_unitFilter->addItem(unit, unit);
    }
    m_unitFilter->setCurrentIndex(qMax(0, m_unitFilter->findData(selectedUnit)));
}

bool PosPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_entry && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            // Enter on an empty field is the shortcut to save.
            if (m_entry->text().trimmed().isEmpty()) {
                completeSale();
                return true;
            }
        } else if (key->key() == Qt::Key_Escape) {
            m_entry->clear();
            return true;
        }
    } else if (watched == m_table && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) {
            onRemoveLine();
            return true;
        }
    } else if (watched == this && event->type() == QEvent::Show) {
        m_entry->setFocus();
    }
    return QWidget::eventFilter(watched, event);
}

int PosPage::lineCount() const
{
    return m_lines.size();
}

long long PosPage::totalCents() const
{
    long long total = 0;
    for (const PosLine& line : m_lines) {
        total += line.unitPriceCents * line.quantity;
    }
    return total;
}

long long PosPage::lineQuantityAt(int row) const
{
    return (row >= 0 && row < m_lines.size()) ? m_lines[row].quantity : 0;
}

long long PosPage::linePriceAt(int row) const
{
    return (row >= 0 && row < m_lines.size()) ? m_lines[row].unitPriceCents : 0;
}

int PosPage::lastSaleId() const
{
    return m_lastSaleId;
}

QString PosPage::noticeText() const
{
    return m_notice->text();
}

void PosPage::setEntryText(const QString& text)
{
    m_entry->setText(text);
}

void PosPage::focusEntry()
{
    m_entry->setFocus();
    // The whole line, so a scan does not land in the middle of a half-typed code
    // from an earlier one. The field is cleared before every scan anyway, but a
    // half-typed name that survived a rejected sale would be appended to here.
    m_entry->selectAll();
}

std::optional<core::Product> PosPage::findProduct(const QString& text) const
{
    data::ProductRepository products(m_db);
    if (const auto byBarcode = products.findByBarcode(text)) {
        return byBarcode;
    }
    for (const core::Product& candidate : products.findAll()) {
        if (candidate.active && candidate.name == text) {
            return candidate;
        }
    }
    return std::nullopt;
}

void PosPage::addProductToCart(const core::Product& product, long long quantity)
{
    for (PosLine& line : m_lines) {
        if (line.productId == product.id) {
            line.quantity += quantity;
            setNotice(tr("Ajouté : %1 × %2")
                          .arg(line.name, formatMoney(line.unitPriceCents)),
                      true);
            rebuildTable();
            m_entry->setFocus();
            return;
        }
    }

    PosLine line;
    line.productId = product.id;
    line.barcode = product.barcode;
    line.name = product.name;
    line.unit = product.unit;
    line.quantity = quantity;
    line.unitPriceCents = product.salePriceCents;
    line.basePriceCents = product.salePriceCents;
    m_lines.append(line);
    setNotice(tr("Ajouté : %1").arg(line.name), true);
    rebuildTable();
    m_entry->setFocus();
}

void PosPage::addEntry()
{
    const QString text = m_entry->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    // Cleared first so a rejected code does not sit in the field waiting to be
    // submitted again.
    m_entry->clear();

    const std::optional<core::Product> product = findProduct(text);
    if (!product) {
        // A code nobody has registered yet is the normal first sale of a new
        // product, so the register offers to create it instead of complaining.
        core::Product draft;
        draft.barcode = text;
        draft.unit = QStringLiteral("piece");
        draft.active = true;

        const std::optional<core::Product> created = showProductDialog(this, m_db, draft);
        if (!created) {
            m_entry->setFocus();
            return;
        }
        const int id = data::ProductRepository(m_db).save(*created);
        if (id == 0) {
            setNotice(tr("Impossible d'enregistrer le produit."), false);
            m_entry->setFocus();
            return;
        }
        // save() hands back the row id rather than filling it in, and the cart
        // matches lines by product id: without this every new product would
        // carry id 0 and the second one would land on the first one's line.
        core::Product saved = *created;
        saved.id = id;
        refreshCatalog();
        m_quickItems->refresh();

        const QString label = saved.name.isEmpty() ? saved.barcode : saved.name;
        const auto answer =
            QMessageBox::question(this, tr("Ajouter à la vente ?"),
                                  tr("Ajouter « %1 » à la vente en cours ?").arg(label),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes) {
            addProductToCart(saved, 1);
            return;
        }
        m_entry->setFocus();
        return;
    }

    addProductToCart(*product, 1);
    m_entry->setFocus();
}

void PosPage::onAddQuickProduct()
{
    core::Product draft;
    draft.unit = QStringLiteral("piece");
    draft.active = true;

    const std::optional<core::Product> created = showProductDialog(this, m_db, draft);
    if (!created) {
        m_entry->setFocus();
        return;
    }
    const int id = data::ProductRepository(m_db).save(*created);
    if (id == 0) {
        setNotice(tr("Impossible d'enregistrer le produit."), false);
        m_entry->setFocus();
        return;
    }
    core::Product saved = *created;
    saved.id = id;
    refreshCatalog();
    m_quickItems->refresh();
    addProductToCart(saved, 1);
}

void PosPage::onQuickItemClicked(int productId)
{
    const std::optional<core::Product> product = data::ProductRepository(m_db).findById(productId);
    if (!product) {
        return;
    }
    addProductToCart(*product, 1);
}

void PosPage::onRemoveLine()
{
    const QList<QTableWidgetSelectionRange> ranges = m_table->selectedRanges();
    if (ranges.isEmpty()) {
        return;
    }
    QSet<int> rows;
    for (const QTableWidgetSelectionRange& range : ranges) {
        for (int row = range.topRow(); row <= range.bottomRow(); ++row) {
            rows.insert(row);
        }
    }
    // Highest row first, so the indices still line up while they are removed.
    QVector<int> toRemove(rows.begin(), rows.end());
    std::sort(toRemove.begin(), toRemove.end(), std::greater<int>());
    for (const int row : toRemove) {
        m_lines.removeAt(row);
    }
    rebuildTable();
    m_entry->setFocus();
}

void PosPage::onClearCart()
{
    if (m_lines.isEmpty()) {
        return;
    }
    const auto answer =
        QMessageBox::question(this, tr("Vider le panier"),
                              tr("Retirer tous les articles du panier ?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
        return;
    }
    m_lines.clear();
    m_notice->clear();
    m_notice->setVisible(false);
    rebuildTable();
    m_entry->setFocus();
}

void PosPage::onCellDoubleClicked(int row, int column)
{
    if (m_updating || row < 0 || row >= m_lines.size()) {
        return;
    }
    if (column == 1) {
        bool accepted = false;
        const int quantity = QInputDialog::getInt(this, tr("Quantité"), tr("Quantité"),
                                                  static_cast<int>(m_lines[row].quantity), 1, 1000000, 1,
                                                  &accepted);
        if (!accepted) {
            return;
        }
        m_table->item(row, column)->setText(QString::number(quantity));
    } else if (column == 2) {
        bool accepted = false;
        const QString price =
            QInputDialog::getText(this, tr("Prix unitaire"), tr("Prix unitaire"),
                                  QLineEdit::Normal, formatMoney(m_lines[row].unitPriceCents), &accepted);
        if (!accepted) {
            return;
        }
        m_table->item(row, column)->setText(price);
    }
}

void PosPage::onCellChanged(int row, int column)
{
    if (m_updating || row < 0 || row >= m_lines.size()) {
        return;
    }
    if (column == 1) {
        bool ok = false;
        const qlonglong quantity = m_table->item(row, column)->text().trimmed().toLongLong(&ok);
        if (!ok || quantity <= 0) {
            rebuildTable();
            return;
        }
        m_lines[row].quantity = quantity;
    } else if (column == 2) {
        const auto cents = parseMoney(m_table->item(row, column)->text());
        if (!cents || *cents <= 0) {
            rebuildTable();
            return;
        }
        m_lines[row].unitPriceCents = *cents;
    } else {
        return;
    }
    rebuildTable();
}

void PosPage::setNotice(const QString& text, bool ok)
{
    m_notice->setObjectName(ok ? QStringLiteral("noticeOk") : QStringLiteral("noticeErr"));
    m_notice->style()->unpolish(m_notice);
    m_notice->style()->polish(m_notice);
    m_notice->update();
    m_notice->setText(text);
    // An empty notice keeps its own height in the layout otherwise, which is
    // what leaves a coloured bar sitting above the page doing nothing.
    m_notice->setVisible(!text.isEmpty());
}

void PosPage::refreshSessionChip()
{
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (session) {
        setChipState(m_sessionChip, QStringLiteral("ok"));
        m_sessionChip->setText(tr("Session ouverte #%1").arg(session->id));
    } else {
        setChipState(m_sessionChip, QStringLiteral("danger"));
        m_sessionChip->setText(tr("Aucune session ouverte"));
    }
}

void PosPage::refreshTotals()
{
    long long total = 0;
    long long units = 0;
    for (const PosLine& line : m_lines) {
        total += line.unitPriceCents * line.quantity;
        units += line.quantity;
    }
    m_countLabel->setText(tr("Articles: %1 | Unités: %2").arg(m_lines.size()).arg(units));
    m_totalLabel->setText(formatMoney(total));
    m_paidLabel->setText(tr("Payé : %1").arg(formatMoney(0)));
    m_remainingLabel->setText(tr("Reste : %1").arg(formatMoney(total)));
}

void PosPage::rebuildTable()
{
    m_updating = true;
    m_table->setRowCount(0);
    for (int i = 0; i < m_lines.size(); ++i) {
        const PosLine& line = m_lines[i];
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        auto* nameItem = new QTableWidgetItem(line.unit.isEmpty() ? line.name
                                                                  : QStringLiteral("%1 / %2").arg(line.name, line.unit));
        nameItem->setData(Qt::UserRole, line.productId);
        m_table->setItem(row, 0, nameItem);

        auto* qtyItem = new QTableWidgetItem(QString::number(line.quantity));
        qtyItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 1, qtyItem);

        auto* priceItem = new QTableWidgetItem(formatMoney(line.unitPriceCents));
        priceItem->setTextAlignment(Qt::AlignLeft);
        // An overridden price is the one thing on this screen that has to be
        // noticed at a glance, since it ends up in the audit log.
        if (line.unitPriceCents != line.basePriceCents) {
            priceItem->setForeground(QBrush(Qt::red));
        }
        m_table->setItem(row, 2, priceItem);

        auto* totalItem = new QTableWidgetItem(formatMoney(line.unitPriceCents * line.quantity));
        totalItem->setTextAlignment(Qt::AlignLeft);
        m_table->setItem(row, 3, totalItem);
    }
    m_updating = false;
    refreshTotals();
}

bool PosPage::syncFromTable()
{
    if (m_lines.size() != m_table->rowCount()) {
        return false;
    }
    bool ok = true;
    for (int i = 0; i < m_lines.size(); ++i) {
        if (!m_table->item(i, 1) || !m_table->item(i, 2)) {
            return false;
        }
        const qlonglong quantity = m_table->item(i, 1)->text().trimmed().toLongLong();
        const auto cents = parseMoney(m_table->item(i, 2)->text());
        if (quantity <= 0 || !cents || *cents < 0) {
            ok = false;
            continue;
        }
        m_lines[i].quantity = quantity;
        m_lines[i].unitPriceCents = *cents;
    }
    return ok;
}

void PosPage::completeSale()
{
    m_notice->clear();
    m_notice->setVisible(false);
    if (m_lines.isEmpty()) {
        setNotice(tr("لا يوجد بنود للبيع"), false);
        return;
    }
    if (!syncFromTable()) {
        setNotice(tr("الكمية أو السعر غير صالح في أحد الأسطر"), false);
        return;
    }

    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        setNotice(tr("لا توجد جلسة مفتوحة — افتح جلسة من قسم \"جلسة الصندوق\" أولاً"), false);
        return;
    }

    QVector<core::SaleItem> items;
    QVector<core::AuditLogEntry> priceOverrides;
    data::AuditLogRepository audit(m_db);
    for (const PosLine& line : m_lines) {
        core::SaleItem item;
        item.productId = line.productId;
        item.quantity = line.quantity;
        item.unitPriceCents = line.unitPriceCents;
        items.append(item);

        if (line.unitPriceCents != line.basePriceCents) {
            core::AuditLogEntry entry;
            entry.actor = app::core::Session::instance().actorName();
            entry.action = QStringLiteral("price_override");
            entry.target = QStringLiteral("%1 (%2): %3 -> %4")
                           .arg(line.name, line.barcode, formatMoney(line.basePriceCents),
                                formatMoney(line.unitPriceCents));
            entry.createdAt = QDateTime::currentDateTime();
            priceOverrides.append(entry);
        }
    }

    data::SaleService service(m_db);
    const data::SaleRecordResult result =
        service.recordSale(items, session->id, app::core::Session::instance().actorName(), /*allowOversold=*/false);
    if (!result.ok) {
        setNotice(tr("تعذر حفظ البيع: %1").arg(result.error), false);
        return;
    }

    m_lastSaleId = result.saleId;
    for (const core::AuditLogEntry& entry : priceOverrides) {
        audit.insert(entry);
    }
    // Kept in Arabic on purpose: tst_ui asserts this exact string.
    setNotice(tr("تم البيع: %1").arg(formatMoney(result.totalCents)), true);
    m_lines.clear();
    rebuildTable();
    refreshCatalog();
    refreshSessionChip();
    m_entry->setFocus();
}

} // namespace app::ui
