#include "pos_page.h"

#include <QBrush>
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
#include <QResizeEvent>
#include <QSet>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QStyle>

#include <algorithm>
#include <functional>

#include "core/barcode_utils.h"
#include "core/session.h"
#include "data/audit_log_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/product_repository.h"
#include "data/sale_service.h"
#include "dialogs/product_dialog.h"
#include "dialogs/select_customer_dialog.h"
#include "format_utils.h"
#include "quick_items_bar.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "theme_tokens.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

PosPage::PosPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* root = new QVBoxLayout(this);
    padPageLayout(root);

    m_entry = new QLineEdit;
    m_entry->setObjectName(QStringLiteral("posBarcodeField"));
    m_entry->setProperty("fullPlaceholder", tr("Code-barres ou nom du produit"));
    m_entry->setToolTip(m_entry->property("fullPlaceholder").toString());
    m_entry->setClearButtonEnabled(true);
    m_entry->setFixedHeight(44);
    QFont entryFont = m_entry->font();
    entryFont.setPointSize(14);
    m_entry->setFont(entryFont);
    m_entry->addAction(appIcon(Icon::Search, QColor(QStringLiteral("#66757a")), 18),
                       QLineEdit::LeadingPosition);

    // The bar that says the sale in progress is going on somebody's account. Built
    // here, hidden, and added to the layout further down so it lands directly above
    // the scan field: it belongs to that field, since it is what decides where the
    // sale the field is feeding ends up.
    m_creditBar = new QFrame;
    m_creditBar->setObjectName(QStringLiteral("creditBar"));
    auto* creditLayout = new QHBoxLayout(m_creditBar);
    creditLayout->setContentsMargins(14, 7, 14, 7);
    creditLayout->setSpacing(14);
    m_creditLabel = new QLabel;
    m_creditLabel->setObjectName(QStringLiteral("creditLabel"));
    auto* creditCancel = new QPushButton(QStringLiteral("×"));
    creditCancel->setObjectName(QStringLiteral("creditCancel"));
    creditCancel->setFixedSize(28, 28);
    creditCancel->setCursor(Qt::PointingHandCursor);
    creditLayout->addWidget(m_creditLabel);
    creditLayout->addStretch();
    creditLayout->addWidget(creditCancel);
    // Nothing to report yet. A layout skips a hidden widget rather than reserving
    // the row, so the till keeps the scan field exactly where it was.
    m_creditBar->setVisible(false);
    connect(creditCancel, &QPushButton::clicked, this, &PosPage::clearCreditMode);

    m_workspace = new QSplitter(Qt::Horizontal);
    m_workspace->setObjectName(QStringLiteral("posWorkspace"));
    m_workspace->setChildrenCollapsible(false);
    m_workspace->setHandleWidth(themeTokens::space4);

    auto* cartPane = new QWidget;
    auto* cartLayout = new QVBoxLayout(cartPane);
    cartLayout->setContentsMargins(0, 0, 0, 0);
    cartLayout->setSpacing(themeTokens::space8);

    // The wide table on the left is the sale itself. It used to be the catalogue,
    // which is what left the cart boxed into the narrow right-hand pane and the
    // cashier counting a bill against a two-column strip; the lines need the room
    // more than the browsing did, so the grid took the wide slot and the stock
    // column it was carrying is replaced by the quantity of the line.
    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("posTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(true);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels(
        {tr("Produit"), tr("Code-barres"), tr("Unité"), tr("Qté"), tr("Prix")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Only the quantity takes a typed edit, and only from the keyboard: F2 or
    // Enter on the current cell. A second tap does not open an inline editor on
    // top of the dialog a double click opens, and a stray keypress can never
    // rewrite a price, which is a number the audit log ends up holding.
    m_table->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(BarcodeColumn, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(UnitColumn, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(QuantityColumn, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(PriceColumn, QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(44);
    m_table->horizontalHeader()->setFixedHeight(34);
    m_table->verticalHeader()->hide();
    m_table->setColumnWidth(BarcodeColumn, 128);
    m_table->setColumnWidth(UnitColumn, 80);
    m_table->setColumnWidth(QuantityColumn, 64);
    m_table->setColumnWidth(PriceColumn, 80);
    // The typography the wide table already had, kept so the grid reads as the
    // same size it read as when it was the catalogue: 14pt for a tablet, with a
    // heavier 12pt header row above it.
    QFont gridFont = m_table->font();
    gridFont.setPointSize(14);
    m_table->setFont(gridFont);
    QFont gridHeaderFont = m_table->horizontalHeader()->font();
    gridHeaderFont.setPointSize(12);
    gridHeaderFont.setBold(true);
    m_table->horizontalHeader()->setFont(gridHeaderFont);
    m_cartStack = new QStackedWidget;
    m_cartStack->addWidget(m_table);
    auto* emptyCart = new QLabel(tr("Scannez un produit ou choisissez-en un dans la liste"));
    emptyCart->setObjectName(QStringLiteral("emptyCartHint"));
    emptyCart->setAlignment(Qt::AlignCenter);
    emptyCart->setWordWrap(true);
    m_cartStack->addWidget(emptyCart);
    m_cartStack->setCurrentWidget(emptyCart);
    cartLayout->addWidget(m_cartStack, 1);

    m_quickItems = new QuickItemsBar(m_db, this);
    cartLayout->addWidget(m_quickItems);

    auto* rightPane = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(themeTokens::space8);

    // The narrow table stays exactly where it was, in the layout, with its header
    // and no rows under it. The +/- buttons that used to sit in a cell of every
    // line are gone with the lines, so nothing in here builds a control any more.
    m_emptyTable = new QTableWidget;
    m_emptyTable->setObjectName(QStringLiteral("posEmptyTable"));
    m_emptyTable->setAlternatingRowColors(true);
    m_emptyTable->setFrameShape(QFrame::NoFrame);
    m_emptyTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_emptyTable->setColumnCount(5);
    m_emptyTable->setHorizontalHeaderLabels(
        {tr("Produit"), tr("Qté"), tr("Prix"), tr("Total"), QString()});
    m_emptyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_emptyTable->horizontalHeader()->setFixedHeight(34);
    m_emptyTable->verticalHeader()->hide();
    rightLayout->addWidget(m_emptyTable, 1);

    auto* clearButton = new QPushButton(tr("Vider"));
    clearButton->setObjectName(QStringLiteral("secondary"));
    clearButton->setIcon(appIcon(Icon::Trash, QColor(QStringLiteral("#475569")), 18));
    clearButton->setText(tr("Vider"));
    clearButton->setToolTip(tr("Vider la facture"));
    auto* removeButton = new QPushButton(tr("Retirer ligne"));
    removeButton->setObjectName(QStringLiteral("secondary"));
    removeButton->setIcon(appIcon(Icon::X, QColor(QStringLiteral("#475569")), 18));
    removeButton->setText(tr("Retirer ligne"));
    removeButton->setToolTip(tr("Retirer la ligne sélectionnée"));

    // ---- the invoice bar, at the foot of the right-hand pane ----
    // One horizontal band rather than the rail it replaces. The total is the
    // largest thing on the page, so it leads and the line count sits under it:
    // reading a sale means reading the money first, then how it was made up.
    auto* invoiceBar = new QFrame;
    invoiceBar->setObjectName(QStringLiteral("invoiceBar"));
    invoiceBar->setMinimumHeight(220);

    auto* totals = new QVBoxLayout;
    totals->setSpacing(0);
    totals->setContentsMargins(0, 0, 0, 0);

    auto* totalCaption = new QLabel(tr("Total"));
    totalCaption->setObjectName(QStringLiteral("heroCaption"));
    totalCaption->setMinimumHeight(24);

    m_totalLabel = new QLabel(QStringLiteral("0.00"));
    m_totalLabel->setObjectName(QStringLiteral("heroValue"));
    m_totalLabel->setMinimumHeight(84);

    m_countLabel = new QLabel;
    m_countLabel->setObjectName(QStringLiteral("heroCaption"));
    m_countLabel->setMinimumHeight(24);

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
    QFont paymentFont = m_paidLabel->font();
    paymentFont.setPointSize(14);
    m_paidLabel->setFont(paymentFont);
    m_remainingLabel->setFont(paymentFont);
    m_paidLabel->setMinimumHeight(24);
    m_remainingLabel->setMinimumHeight(24);
    paymentSummary->addWidget(m_paidLabel);
    paymentSummary->addWidget(m_remainingLabel);

    m_save = new QPushButton(tr("Enregistrer la vente"));
    m_save->setObjectName(QStringLiteral("posSaleButton"));
    m_save->setMinimumHeight(56);
    m_save->setIcon(appIcon(Icon::Check, QColor(QStringLiteral("#ffffff")), 20));
    m_save->setText(tr("Valider la vente (Entrée)"));
    m_save->setToolTip(tr("Valider la vente (Entrée)"));
    QFont saveFont = m_save->font();
    saveFont.setPointSize(16);
    saveFont.setBold(true);
    m_save->setFont(saveFont);

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

    auto* secondaryRow = new QHBoxLayout;
    secondaryRow->setContentsMargins(0, 0, 0, 0);
    secondaryRow->setSpacing(themeTokens::space8);
    removeButton->setMinimumHeight(44);
    clearButton->setMinimumHeight(44);
    secondaryRow->addWidget(removeButton, 1);
    secondaryRow->addWidget(clearButton, 1);
    barLayout->addWidget(m_save);
    barLayout->addLayout(secondaryRow);

    rightLayout->addWidget(invoiceBar);

    // The scan field is the only control above the panes now: the catalogue
    // search that shared the row went with the catalogue it filtered. The credit
    // bar goes above it rather than below, so the eye reads the account before the
    // barcode rather than after it.
    root->addWidget(m_creditBar);
    root->addWidget(m_entry);

    m_workspace->addWidget(cartPane);
    m_workspace->addWidget(rightPane);
    rightPane->setMinimumWidth(400);
    m_workspace->setStretchFactor(0, 60);
    m_workspace->setStretchFactor(1, 40);
    root->addWidget(m_workspace, 1);

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

    // The filter is installed on the page itself as well as on the entry field,
    // because the entry is not the only thing it watches: the page's own Show
    // event is what tells us the register has become the visible page, and that
    // has to reach the entry for the cashier's next scan.
    m_entry->installEventFilter(this);
    installEventFilter(this);
    m_table->installEventFilter(this);

    refreshTotals();
    refreshQuickItems();
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

void PosPage::refreshQuickItems()
{
    m_quickItems->refresh();
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

void PosPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // The field advertises both a barcode and a name, which is longer than the
    // box; what it cannot show is replaced by an ellipsis rather than cut off
    // mid-word.
    m_entry->setPlaceholderText(QFontMetrics(m_entry->font()).elidedText(
        m_entry->property("fullPlaceholder").toString(), Qt::ElideRight,
        qMax(0, m_entry->contentsRect().width() - 36)));
    if (!m_workspace) {
        return;
    }
    const int available = m_workspace->width();
    const int cartWidth = qMax(400, available * 40 / 100);
    m_workspace->setSizes({qMax(0, available - cartWidth), cartWidth});
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
        refreshQuickItems();

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
    refreshQuickItems();
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
    // Two cells answer a double click, each with the dialog a tablet can put a
    // number pad behind: the quantity of the line, and its unit price, which a
    // cashier may override and which the audit log records when they do.
    if (column == QuantityColumn) {
        bool accepted = false;
        const int quantity = QInputDialog::getInt(this, tr("Quantité"), tr("Quantité"),
                                                  static_cast<int>(m_lines[row].quantity), 1, 1000000, 1,
                                                  &accepted);
        if (!accepted) {
            return;
        }
        m_lines[row].quantity = quantity;
        m_table->item(row, column)->setText(QString::number(quantity));
        refreshTotals();
    } else if (column == PriceColumn) {
        bool accepted = false;
        const QString price =
            QInputDialog::getText(this, tr("Prix unitaire"), tr("Prix unitaire"),
                                  QLineEdit::Normal, formatMoney(m_lines[row].unitPriceCents), &accepted);
        if (!accepted) {
            return;
        }
        const auto cents = parseMoney(price);
        if (!cents || *cents <= 0) {
            return;
        }
        m_lines[row].unitPriceCents = *cents;
        rebuildTable();
    }
}

void PosPage::onCellChanged(int row, int column)
{
    if (m_updating || row < 0 || row >= m_lines.size()) {
        return;
    }
    // Only the quantity is editable in place. The price is left alone here on
    // purpose: a stray keypress that lands in a price cell would rewrite a number
    // that ends up in the audit log, so it has to go through the dialog above.
    if (column != QuantityColumn) {
        return;
    }
    QTableWidgetItem* item = m_table->item(row, column);
    if (!item) {
        return;
    }
    bool ok = false;
    const qlonglong quantity = item->text().trimmed().toLongLong(&ok);
    if (!ok || quantity <= 0) {
        // Put back what the line really holds rather than keeping a number no
        // sale could be made from.
        item->setText(QString::number(m_lines[row].quantity));
        return;
    }
    m_lines[row].quantity = quantity;
    // The grid is not rebuilt here: nothing on it depends on the quantity, the
    // total and the unit count in the invoice bar are the only things that move.
    item->setText(QString::number(quantity));
    refreshTotals();
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
    m_save->setEnabled(!m_lines.isEmpty());
    m_save->setToolTip(m_lines.isEmpty()
                           ? tr("Scannez un produit ou choisissez-en un dans la liste")
                           : tr("Valider la vente (Entrée)"));
}

void PosPage::rebuildTable()
{
    m_updating = true;
    m_table->setRowCount(0);
    for (int i = 0; i < m_lines.size(); ++i) {
        const PosLine& line = m_lines[i];
        const int row = m_table->rowCount();
        m_table->insertRow(row);

        // A QTableWidget item is editable by default, so every cell that is not
        // the quantity has to say so itself. The quantity is the one cell that
        // opens for typing, which is what makes it changeable from a keyboard
        // attached to a till; F2 or Enter starts the editor, and a double click
        // opens the dialog in onCellDoubleClicked.
        const auto readOnly = [](QTableWidgetItem* item) {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        };

        auto* nameItem = new QTableWidgetItem(line.name);
        nameItem->setData(Qt::UserRole, line.productId);
        readOnly(nameItem);
        m_table->setItem(row, NameColumn, nameItem);

        auto* barcodeItem = new QTableWidgetItem(line.barcode);
        barcodeItem->setTextAlignment(Qt::AlignCenter);
        readOnly(barcodeItem);
        m_table->setItem(row, BarcodeColumn, barcodeItem);

        auto* unitItem = new QTableWidgetItem(line.unit);
        unitItem->setTextAlignment(Qt::AlignCenter);
        readOnly(unitItem);
        m_table->setItem(row, UnitColumn, unitItem);

        auto* qtyItem = new QTableWidgetItem(QString::number(line.quantity));
        qtyItem->setTextAlignment(Qt::AlignCenter);
        qtyItem->setFlags(qtyItem->flags() | Qt::ItemIsEditable);
        m_table->setItem(row, QuantityColumn, qtyItem);

        auto* priceItem = new QTableWidgetItem(formatMoney(line.unitPriceCents));
        priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // An overridden price is the one thing on this screen that has to be
        // noticed at a glance, since it ends up in the audit log.
        if (line.unitPriceCents != line.basePriceCents) {
            priceItem->setForeground(QBrush(Qt::red));
        }
        readOnly(priceItem);
        m_table->setItem(row, PriceColumn, priceItem);
    }
    m_updating = false;
    m_cartStack->setCurrentWidget(m_lines.isEmpty() ? m_cartStack->widget(1) : m_table);
    refreshTotals();
}

bool PosPage::syncFromTable()
{
    if (m_lines.size() != m_table->rowCount()) {
        return false;
    }
    bool ok = true;
    for (int i = 0; i < m_lines.size(); ++i) {
        if (!m_table->item(i, QuantityColumn) || !m_table->item(i, PriceColumn)) {
            return false;
        }
        const qlonglong quantity = m_table->item(i, QuantityColumn)->text().trimmed().toLongLong();
        const auto cents = parseMoney(m_table->item(i, PriceColumn)->text());
        if (quantity <= 0 || !cents || *cents < 0) {
            ok = false;
            continue;
        }
        m_lines[i].quantity = quantity;
        m_lines[i].unitPriceCents = *cents;
    }
    return ok;
}

void PosPage::enterCreditMode()
{
    const std::optional<int> customerId = showSelectCustomerDialog(this, m_db);
    if (!customerId) {
        return;
    }
    data::CustomerRepository repo(m_db);
    const auto customer = repo.findById(*customerId);
    if (!customer) {
        return;
    }
    m_creditCustomerId = *customerId;
    // The balance belongs on the bar, not only in the picker that was just closed:
    // this is the figure the cashier is about to add to, and a sale that pushes an
    // account further under is the whole reason to be looking at it.
    const long long balance = repo.balanceCentsFor(*customerId);
    m_creditLabel->setText(tr("À CRÉDIT : %1 · Solde : %2")
                               .arg(customer->name, formatMoney(balance)));
    m_creditBar->setVisible(true);
    m_save->setText(tr("Enregistrer à crédit (Entrée)"));
    m_entry->setFocus();
}

void PosPage::clearCreditMode()
{
    m_creditCustomerId = 0;
    m_creditBar->setVisible(false);
    // Back to the wording the button was built with. Written out rather than left
    // alone, so the till label is the same string whether the mode was just entered
    // or was never entered at all.
    m_save->setText(tr("Valider la vente (Entrée)"));
    m_entry->setFocus();
}

void PosPage::completeSale()
{
    m_notice->clear();
    m_notice->setVisible(false);
    if (m_lines.isEmpty()) {
        return;
    }
    if (!syncFromTable()) {
        setNotice(tr("الكمية أو السعر غير صالح في أحد الأسطر"), false);
        return;
    }

    // A credit sale never reached the drawer, so it is recorded without one. Asking
    // for a session here would refuse a sale that is perfectly recordable on the
    // grounds that the till happens to be shut.
    std::optional<core::CashSession> session;
    if (m_creditCustomerId <= 0) {
        data::CashSessionRepository sessions(m_db);
        session = sessions.findOpen();
        if (!session) {
            setNotice(tr("لا توجد جلسة مفتوحة — افتح جلسة من قسم \"جلسة الصندوق\" أولاً"), false);
            return;
        }
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

    // The account was named above the scan field, so this sale is written to that
    // customer's ledger rather than to the till. Same lines, same service, one
    // argument apart.
    if (m_creditCustomerId > 0) {
        const data::SaleRecordResult result = service.recordCustomerDebt(
            m_creditCustomerId, items, app::core::Session::instance().actorName(),
            /*allowOversold=*/false);
        if (!result.ok) {
            setNotice(tr("تعذر تسجيل الدين: %1").arg(result.error), false);
            return;
        }
        // m_lastSaleId is left alone on purpose: what came back is a ledger
        // transaction id, and lastSaleId() is read as a row of the sales table.
        // An overridden price is still an overridden price, on this side too.
        for (const core::AuditLogEntry& entry : priceOverrides) {
            audit.insert(entry);
        }
        setNotice(tr("تم تسجيل الدين : %1").arg(formatMoney(result.totalCents)), true);
        clearCreditMode();
        m_lines.clear();
        rebuildTable();
        refreshQuickItems();
        return;
    }

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
    refreshQuickItems();
    m_entry->setFocus();
}

} // namespace app::ui
