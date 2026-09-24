#include "sales_page.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTime>
#include <QVBoxLayout>

#include <algorithm>

#include "data/product_repository.h"
#include "data/sale_item_repository.h"
#include "data/sale_repository.h"
#include "format_utils.h"

namespace app::ui {

namespace {

QString deviceLabel(const QString& deviceId)
{
    if (deviceId == QLatin1String("desktop")) {
        return QStringLiteral("الحاسوب");
    }
    if (deviceId.isEmpty()) {
        return QStringLiteral("—");
    }
    return QStringLiteral("جهاز %1").arg(deviceId.left(8));
}

long long cogsFor(int saleId, app::data::SaleItemRepository& items)
{
    long long cogs = 0;
    for (const core::SaleItem& item : items.findBySaleId(saleId)) {
        cogs += item.unitCostCents * item.quantity;
    }
    return cogs;
}

} // namespace

SalesPage::SalesPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(QStringLiteral("font-weight: bold;"));

    m_table = new QTableWidget;
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("الوقت"), QStringLiteral("المصدر"), QStringLiteral("الإجمالي"), QStringLiteral("الحالة")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setColumnWidth(0, 90);
    m_table->setColumnWidth(2, 130);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_summary);
    layout->addWidget(m_table);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, &SalesPage::showDetails);

    refresh();
}

void SalesPage::refresh()
{
    data::SaleRepository sales(m_db);
    data::SaleItemRepository saleItems(m_db);

    const QDateTime now = QDateTime::currentDateTime();
    QDateTime dayStart = now;
    dayStart.setTime(QTime(0, 0, 0));

    const auto all = sales.findBetween(dayStart, now);
    std::vector<core::Sale> ordered(all.begin(), all.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const core::Sale& a, const core::Sale& b) { return a.createdAt > b.createdAt; });

    m_table->setRowCount(0);
    long long total = 0;
    long long cogs = 0;
    for (const core::Sale& sale : ordered) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(sale.createdAt.toString(QStringLiteral("HH:mm:ss"))));
        m_table->setItem(row, 1, new QTableWidgetItem(deviceLabel(sale.deviceId)));
        m_table->setItem(row, 2, new QTableWidgetItem(formatMoney(sale.totalCents)));
        m_table->setItem(row, 3,
                         new QTableWidgetItem(sale.reversedSaleId != 0 ? QStringLiteral("مسترد")
                                                                       : QStringLiteral("بيع")));
        m_table->item(row, 0)->setData(Qt::UserRole, sale.id);

        total += sale.totalCents;
        if (sale.reversedSaleId == 0) {
            cogs += cogsFor(sale.id, saleItems);
        }
    }

    m_summary->setText(QStringLiteral("مبيعات اليوم: %1  |  الإجمالي (الصافي): %2  |  الربح التقريبي: %3")
                           .arg(ordered.size())
                           .arg(formatMoney(total))
                           .arg(formatMoney(total - cogs)));
}

int SalesPage::rowCount() const
{
    return m_table->rowCount();
}

long long SalesPage::grandTotalCents() const
{
    long long total = 0;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        total += parseMoney(m_table->item(i, 2)->text()).value_or(0);
    }
    return total;
}

long long SalesPage::profitCents() const
{
    // Parsed back out of the summary label for simplicity; grandTotal is exact.
    const QRegularExpression totalRe(QStringLiteral("الإجمالي \\(الصافي\\): (-?[0-9.]+)"));
    const auto match = totalRe.match(m_summary->text());
    long long total = 0;
    if (match.hasMatch()) {
        total = parseMoney(match.captured(1)).value_or(0);
    }
    const QRegularExpression profitRe(QStringLiteral("الربح التقريبي: (-?[0-9.]+)"));
    const auto profitMatch = profitRe.match(m_summary->text());
    return profitMatch.hasMatch() ? parseMoney(profitMatch.captured(1)).value_or(0) : total;
}

void SalesPage::showDetails()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        return;
    }
    const int saleId = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    data::SaleRepository sales(m_db);
    const auto sale = sales.findById(saleId);
    if (!sale) {
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("تفاصيل البيع #%1 (%2)").arg(sale->id).arg(formatMoney(sale->totalCents)));
    dialog.setModal(true);

    auto* items = new QTableWidget;
    items->setColumnCount(4);
    items->setHorizontalHeaderLabels(
        {QStringLiteral("المنتج"), QStringLiteral("الكمية"), QStringLiteral("سعر الوحدة"), QStringLiteral("الإجمالي")});
    items->setEditTriggers(QAbstractItemView::NoEditTriggers);
    items->horizontalHeader()->setStretchLastSection(true);

    data::ProductRepository products(m_db);
    data::SaleItemRepository saleItems(m_db);
    for (const core::SaleItem& item : saleItems.findBySaleId(saleId)) {
        const auto product = products.findById(item.productId);
        const QString name = product ? product->name : QStringLiteral("(#%1)").arg(item.productId);
        const int r = items->rowCount();
        items->insertRow(r);
        items->setItem(r, 0, new QTableWidgetItem(name));
        items->setItem(r, 1, new QTableWidgetItem(QString::number(item.quantity)));
        items->setItem(r, 2, new QTableWidgetItem(formatMoney(item.unitPriceCents)));
        items->setItem(r, 3, new QTableWidgetItem(formatMoney(item.unitPriceCents * item.quantity)));
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(items);
    layout->addWidget(buttons);
    dialog.exec();
}

} // namespace app::ui