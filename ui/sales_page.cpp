#include "sales_page.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTime>
#include <QVBoxLayout>

#include <algorithm>

#include "data/product_repository.h"
#include "data/sale_item_repository.h"
#include "data/sale_repository.h"
#include "format_utils.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/stat_card.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

QString deviceLabel(const QString& deviceId)
{
    if (deviceId == QLatin1String("desktop")) {
        return QCoreApplication::translate("app::ui::SalesPage", "الحاسوب");
    }
    if (deviceId.isEmpty()) {
        return QCoreApplication::translate("app::ui::SalesPage", "—");
    }
    return QCoreApplication::translate("app::ui::SalesPage", "جهاز %1").arg(deviceId.left(8));
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
    auto* header = new PageHeader(tr("المبيعات"),
                                  tr("سجل مبيعات اليوم مصنّفاً حسب الجهاز"));
    m_countCard = new StatCard(tr("فواتير اليوم"));
    m_countCard->setIcon(Icon::Receipt, QStringLiteral("#c8860f"));
    m_totalCard = new StatCard(tr("الإجمالي (الصافي)"));
    m_totalCard->setIcon(Icon::Wallet, QStringLiteral("#0e7c75"));
    m_profitCard = new StatCard(tr("الربح التقريبي"));
    m_profitCard->setIcon(Icon::BarChart, QStringLiteral("#1d5f9e"));

    auto* cards = new QHBoxLayout;
    cards->setSpacing(10);
    cards->addWidget(m_countCard, 1);
    cards->addWidget(m_totalCard, 1);
    cards->addWidget(m_profitCard, 1);

    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    m_summary->setObjectName(QStringLiteral("infoBar"));
    m_summary->setMinimumHeight(48);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("salesTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(false);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("الوقت"), tr("المصدر"), tr("الإجمالي"), tr("الحالة")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setDefaultSectionSize(42);
    m_table->setColumnWidth(0, 90);
    m_table->setColumnWidth(2, 130);

    auto* tableCard = makeCard();
    auto* tableLayout = new QVBoxLayout(tableCard);
    tableLayout->setContentsMargins(18, 16, 18, 16);
    tableLayout->setSpacing(10);
    tableLayout->addWidget(makeCardTitle(tr("سجل فواتير اليوم")));
    tableLayout->addWidget(m_table, 1);
    tableLayout->addWidget(m_summary);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(header);
    root->addLayout(cards);
    root->addWidget(tableCard, 1);

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
                         new QTableWidgetItem(sale.reversedSaleId != 0 ? tr("مسترد")
                                                                       : tr("بيع")));
        m_table->item(row, 0)->setData(Qt::UserRole, sale.id);

        total += sale.totalCents;
        if (sale.reversedSaleId == 0) {
            cogs += cogsFor(sale.id, saleItems);
        }
    }

    m_countCard->setValue(QString::number(ordered.size()));
    m_totalCard->setCents(total);
    m_profitCard->setDelta(total - cogs);

    // Cache the figures before rendering: grandTotalCents()/profitCents() must not
    // scrape them back out of the (translatable) summary text.
    m_cachedGrandTotalCents = total;
    m_cachedProfitCents = total - cogs;

    m_summary->setText(tr("مبيعات اليوم: %1  |  الإجمالي (الصافي): %2  |  الربح التقريبي: %3")
                           .arg(ordered.size())
                           .arg(formatMoney(total))
                           .arg(formatMoney(total - cogs)));
}

int SalesPage::rowCount() const
{
    return m_table->rowCount();
}

qint64 SalesPage::grandTotalCents() const
{
    return m_cachedGrandTotalCents;
}

qint64 SalesPage::profitCents() const
{
    return m_cachedProfitCents;
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
    dialog.setWindowTitle(tr("تفاصيل البيع #%1 (%2)").arg(sale->id).arg(formatMoney(sale->totalCents)));
    dialog.setModal(true);

    auto* items = new QTableWidget;
    items->setColumnCount(4);
    items->setHorizontalHeaderLabels(
        {tr("المنتج"), tr("الكمية"), tr("سعر الوحدة"), tr("الإجمالي")});
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