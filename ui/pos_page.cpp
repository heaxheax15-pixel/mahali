#include "pos_page.h"

#include <QBrush>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "data/audit_log_repository.h"
#include "data/cash_session_repository.h"
#include "data/product_repository.h"
#include "data/sale_service.h"
#include "format_utils.h"

namespace app::ui {

PosPage::PosPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_entry = new QLineEdit;
    m_entry->setPlaceholderText(QStringLiteral("باركود أو اسم المنتج — Enter يضيف، Enter فارغ يحفظ البيع"));
    m_entry->setClearButtonEnabled(true);

    m_save = new QPushButton(QStringLiteral("حفظ البيع"));
    m_save->setMinimumHeight(40);

    m_table = new QTableWidget;
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("المنتج"), QStringLiteral("الكمية"), QStringLiteral("سعر الوحدة"), QStringLiteral("الإجمالي")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked
                             | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setColumnWidth(1, 90);
    m_table->setColumnWidth(2, 120);

    m_itemsLabel = new QLabel;
    m_totalLabel = new QLabel;
    m_totalLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: bold;"));
    m_notice = new QLabel;
    m_notice->setWordWrap(true);

    auto* footer = new QHBoxLayout;
    footer->addWidget(m_itemsLabel);
    footer->addStretch(1);
    footer->addWidget(m_totalLabel);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_entry);
    layout->addWidget(m_table);
    layout->addLayout(footer);
    layout->addWidget(m_save);
    layout->addWidget(m_notice);

    connect(m_entry, &QLineEdit::returnPressed, this, &PosPage::addEntry);
    connect(m_save, &QPushButton::clicked, this, &PosPage::completeSale);
    connect(m_table, &QTableWidget::cellChanged, this, &PosPage::onCellChanged);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &PosPage::refreshTotals);

    m_entry->installEventFilter(this);
    m_table->installEventFilter(this);

    refreshTotals();
    m_entry->setFocus();
}

bool PosPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_entry && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
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

void PosPage::addEntry()
{
    const QString text = m_entry->text().trimmed();
    if (text.isEmpty()) {
        return;
    }
    m_entry->clear();

    data::ProductRepository products(m_db);
    std::optional<core::Product> product = products.findByBarcode(text);
    if (!product) {
        for (const core::Product& candidate : products.findAll()) {
            if (candidate.active && candidate.name == text) {
                product = candidate;
                break;
            }
        }
    }
    if (!product) {
        m_notice->setText(QStringLiteral("لا يوجد منتج بالباركود/الاسم: %1").arg(text));
        return;
    }

    for (PosLine& line : m_lines) {
        if (line.productId == product->id) {
            ++line.quantity;
            m_notice->setText(QStringLiteral("أضيف: %1 × تقييم %2")
                                  .arg(line.name, formatMoney(line.unitPriceCents)));
            rebuildTable();
            m_entry->setFocus();
            return;
        }
    }

    PosLine line;
    line.productId = product->id;
    line.barcode = product->barcode;
    line.name = product->name;
    line.unit = product->unit;
    line.quantity = 1;
    line.unitPriceCents = product->salePriceCents;
    line.basePriceCents = product->salePriceCents;
    m_lines.append(line);
    m_notice->setText(QStringLiteral("أضيف: %1").arg(line.name));
    rebuildTable();
    m_entry->setFocus();
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
    QVector<int> toRemove(rows.begin(), rows.end());
    std::sort(toRemove.begin(), toRemove.end(), std::greater<int>());
    for (const int row : toRemove) {
        m_lines.removeAt(row);
    }
    rebuildTable();
    m_entry->setFocus();
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
        if (!cents || *cents < 0) {
            rebuildTable();
            return;
        }
        m_lines[row].unitPriceCents = *cents;
    } else {
        return;
    }
    rebuildTable();
}

void PosPage::refreshTotals()
{
    long long total = 0;
    long long items = 0;
    for (const PosLine& line : m_lines) {
        total += line.unitPriceCents * line.quantity;
        items += line.quantity;
    }
    m_itemsLabel->setText(QStringLiteral("الأصناف: %1  |  القطع: %2").arg(m_lines.size()).arg(items));
    m_totalLabel->setText(QStringLiteral("الإجمالي: %1").arg(formatMoney(total)));
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
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setData(Qt::UserRole, line.productId);
        m_table->setItem(row, 0, nameItem);

        auto* qtyItem = new QTableWidgetItem(QString::number(line.quantity));
        qtyItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 1, qtyItem);

        auto* priceItem = new QTableWidgetItem(formatMoney(line.unitPriceCents));
        priceItem->setTextAlignment(Qt::AlignLeft);
        if (line.unitPriceCents != line.basePriceCents) {
            priceItem->setForeground(QBrush(Qt::red));
        }
        m_table->setItem(row, 2, priceItem);

        auto* totalItem = new QTableWidgetItem(formatMoney(line.unitPriceCents * line.quantity));
        totalItem->setTextAlignment(Qt::AlignLeft);
        totalItem->setFlags(totalItem->flags() & ~Qt::ItemIsEditable);
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
    if (m_lines.isEmpty()) {
        m_notice->setText(QStringLiteral("لا يوجد بنود للبيع"));
        return;
    }
    if (!syncFromTable()) {
        m_notice->setText(QStringLiteral("الكمية أو السعر غير صالح في أحد الأسطر"));
        return;
    }

    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(QStringLiteral("لا توجد جلسة مفتوحة — افتح جلسة من قسم \"جلسة الصندوق\" أولاً"));
        return;
    }

    QVector<core::SaleItem> items;
    data::AuditLogRepository audit(m_db);
    for (const PosLine& line : m_lines) {
        core::SaleItem item;
        item.productId = line.productId;
        item.quantity = line.quantity;
        item.unitPriceCents = line.unitPriceCents;
        items.append(item);

        if (line.unitPriceCents != line.basePriceCents) {
            core::AuditLogEntry entry;
            entry.actor = QStringLiteral("desktop");
            entry.action = QStringLiteral("price_override");
            entry.target = QStringLiteral("%1 (%2): %3 -> %4")
                               .arg(line.name, line.barcode, formatMoney(line.basePriceCents),
                                    formatMoney(line.unitPriceCents));
            entry.createdAt = QDateTime::currentDateTime();
            audit.insert(entry);
        }
    }

    data::SaleService service(m_db);
    const data::SaleRecordResult result =
        service.recordSale(items, session->id, QStringLiteral("desktop"), /*allowOversold=*/false);
    if (!result.ok) {
        m_notice->setText(QStringLiteral("تعذر حفظ البيع: %1").arg(result.error));
        return;
    }

    m_lastSaleId = result.saleId;
    m_notice->setText(QStringLiteral("تم البيع: %1").arg(formatMoney(result.totalCents)));
    m_lines.clear();
    rebuildTable();
    m_entry->setFocus();
}

} // namespace app::ui