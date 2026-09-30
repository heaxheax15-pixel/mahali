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
#include <QSet>
#include <QSignalBlocker>
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
#include "widgets/ui_helpers.h"

namespace app::ui {

PosPage::PosPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* root = new QVBoxLayout(this);
    padPageLayout(root);

    auto* header = new PageHeader(tr("Vente rapide"), tr("Code-barres ou nom du produit"));
    m_sessionChip = makeChip(tr("Session"), QStringLiteral("info"));
    header->addAction(m_sessionChip);
    root->addWidget(header);

    // The barcode field is the only thing that has to be reachable without
    // touching the mouse, so it is the tallest control on the page. The inline
    // rule overrides the app-wide #searchField min-height so the box really
    // lands on 56px instead of the 44px+padding the theme would ask for.
    m_entry = new QLineEdit;
    m_entry->setObjectName(QStringLiteral("searchField"));
    m_entry->setPlaceholderText(tr("Code-barres ou nom du produit"));
    m_entry->setClearButtonEnabled(true);
    m_entry->setStyleSheet(QStringLiteral("min-height: 36px; padding: 10px 14px; font-size: 18px;"));
    m_entry->setFixedHeight(56);
    m_entry->addAction(appIcon(Icon::Search, QColor(QStringLiteral("#66757a")), 18),
                       QLineEdit::LeadingPosition);
    root->addWidget(m_entry);

    // Quick items are the products that cannot be scanned, so they get a
    // permanent strip under the entry instead of a search box of their own.
    m_quickItems = new QuickItemsBar(m_db, this);
    m_quickItems->setFixedHeight(120);
    root->addWidget(m_quickItems);

    auto* body = new QHBoxLayout;
    body->setSpacing(16);

    // ---- cart, on the wide side ----
    auto* cart = new QWidget;
    auto* cartLayout = new QVBoxLayout(cart);
    cartLayout->setContentsMargins(0, 0, 0, 0);
    cartLayout->setSpacing(12);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("posTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(false);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({tr("Produit"), tr("Qté"), tr("PU"), tr("Total")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Qty and PU are edited through QInputDialog on double click, which is the
    // only way to get a number pad on a tablet. Leaving the items editable as
    // well would let a stray keypress silently rewrite a price.
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setDefaultSectionSize(46);
    m_table->setColumnWidth(1, 90);
    m_table->setColumnWidth(2, 120);
    cartLayout->addWidget(m_table, 1);

    auto* cartActions = new QHBoxLayout;
    cartActions->setSpacing(12);

    auto* clearButton = new QPushButton(tr("Vider"));
    clearButton->setObjectName(QStringLiteral("secondary"));
    auto* removeButton = new QPushButton(tr("Retirer ligne"));
    removeButton->setObjectName(QStringLiteral("secondary"));

    cartActions->addWidget(clearButton);
    cartActions->addWidget(removeButton);
    cartLayout->addLayout(cartActions);

    body->addWidget(cart, 3);

    // ---- invoice, fixed rail on the right ----
    auto* invoice = new QFrame;
    invoice->setObjectName(QStringLiteral("card"));
    invoice->setFixedWidth(320);
    auto* invoiceLayout = new QVBoxLayout(invoice);
    invoiceLayout->setContentsMargins(18, 18, 18, 18);
    invoiceLayout->setSpacing(8);

    auto* totalCaption = new QLabel(tr("Total"));
    totalCaption->setObjectName(QStringLiteral("heroCaption"));
    // #heroCaption is 13px in the theme; only the size is overridden here, the
    // muted colour still comes from the object name rule.
    totalCaption->setStyleSheet(QStringLiteral("font-size: 12px;"));

    m_totalLabel = new QLabel(QStringLiteral("0.00"));
    m_totalLabel->setObjectName(QStringLiteral("heroValue"));
    m_totalLabel->setStyleSheet(QStringLiteral("font-size: 36px;"));
    m_totalLabel->setAlignment(Qt::AlignCenter);

    m_countLabel = new QLabel;
    m_countLabel->setObjectName(QStringLiteral("heroCaption"));
    m_countLabel->setAlignment(Qt::AlignCenter);

    m_save = new QPushButton(tr("Enregistrer la vente"));
    m_save->setObjectName(QStringLiteral("primary"));
    m_save->setFixedHeight(52);
    m_save->setIcon(appIcon(Icon::Check, QColor(QStringLiteral("#ffffff")), 20));

    invoiceLayout->addStretch(1);
    invoiceLayout->addWidget(totalCaption, 0, Qt::AlignHCenter);
    invoiceLayout->addWidget(m_totalLabel);
    invoiceLayout->addWidget(m_countLabel);
    invoiceLayout->addStretch(1);
    invoiceLayout->addWidget(m_save);

    body->addWidget(invoice);

    root->addLayout(body, 1);

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

    m_entry->installEventFilter(this);
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
    refreshSessionChip();
    m_entry->setFocus();
}

} // namespace app::ui
