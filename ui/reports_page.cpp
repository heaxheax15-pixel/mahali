#include "reports_page.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateEdit>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

#include "data/report_service.h"
#include "format_utils.h"

namespace app::ui {

namespace {

QString cashTypeLabel(const QString& type)
{
    if (type == QLatin1String("sale")) {
        return QCoreApplication::translate("app::ui::ReportsPage", "بيع");
    }
    if (type == QLatin1String("customer_payment")) {
        return QCoreApplication::translate("app::ui::ReportsPage", "دفع عميل");
    }
    if (type == QLatin1String("expense")) {
        return QCoreApplication::translate("app::ui::ReportsPage", "مصروف");
    }
    if (type == QLatin1String("drawing")) {
        return QCoreApplication::translate("app::ui::ReportsPage", "سحب مالك");
    }
    if (type == QLatin1String("refund")) {
        return QCoreApplication::translate("app::ui::ReportsPage", "استرداد");
    }
    return type;
}

QDateTime startOfDay(const QDate& date)
{
    return QDateTime(date, QTime(0, 0, 0));
}

QDateTime endOfDay(const QDate& date)
{
    return startOfDay(date.addDays(1)).addMSecs(-1);
}

} // namespace

ReportsPage::ReportsPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_fromEdit = new QDateEdit;
    m_fromEdit->setCalendarPopup(true);
    m_fromEdit->setMinimumHeight(42);
    m_toEdit = new QDateEdit;
    m_toEdit->setCalendarPopup(true);
    m_toEdit->setMinimumHeight(42);

    auto* todayButton = new QPushButton(tr("اليوم"));
    auto* yesterdayButton = new QPushButton(tr("أمس"));
    auto* weekButton = new QPushButton(tr("هذا الأسبوع"));
    auto* monthButton = new QPushButton(tr("هذا الشهر"));
    auto* allButton = new QPushButton(tr("الكل"));
    for (QPushButton* button : {todayButton, yesterdayButton, weekButton, monthButton, allButton}) {
        button->setObjectName(QStringLiteral("secondary"));
    }

    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    m_summary->setObjectName(QStringLiteral("infoBar"));
    m_summary->setMinimumHeight(50);

    m_costs = new QLabel;
    m_costs->setWordWrap(true);
    m_bottom = new QLabel;
    m_bottom->setWordWrap(true);
    m_costs->setObjectName(QStringLiteral("faintText"));
    m_bottom->setObjectName(QStringLiteral("faintText"));

    m_cashTable = new QTableWidget;
    m_cashTable->setObjectName(QStringLiteral("reportTable"));
    m_cashTable->setAlternatingRowColors(true);
    m_cashTable->setFrameShape(QFrame::NoFrame);
    m_cashTable->setShowGrid(false);
    m_cashTable->setColumnCount(3);
    m_cashTable->setHorizontalHeaderLabels(
        {tr("العملية"), tr("العدد"), tr("المجموع")});
    m_cashTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_cashTable->horizontalHeader()->setStretchLastSection(true);
    m_cashTable->verticalHeader()->setDefaultSectionSize(42);

    auto* quick = new QHBoxLayout;
    quick->setSpacing(10);
    for (QPushButton* button : {todayButton, yesterdayButton, weekButton, monthButton, allButton}) {
        quick->addWidget(button);
    }
    quick->addStretch(1);

    auto* range = new QHBoxLayout;
    range->setSpacing(10);
    range->addWidget(m_fromEdit);
    range->addWidget(new QLabel(tr("إلى:")));
    range->addWidget(m_toEdit);
    range->addStretch(1);

    auto* summaryCard = makeCard();
    auto* summaryLayout = new QVBoxLayout(summaryCard);
    summaryLayout->setContentsMargins(18, 16, 18, 16);
    summaryLayout->setSpacing(10);
    summaryLayout->addWidget(makeCardTitle(tr("الملخص")));
    summaryLayout->addWidget(m_summary);
    summaryLayout->addWidget(m_costs);
    summaryLayout->addWidget(m_bottom);

    auto* tableCard = makeCard();
    auto* tableLayout = new QVBoxLayout(tableCard);
    tableLayout->setContentsMargins(18, 16, 18, 16);
    tableLayout->setSpacing(10);
    tableLayout->addWidget(makeCardTitle(tr("تفاصيل العمليات")));
    tableLayout->addWidget(m_cashTable, 1);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(tr("التقارير"),
                                   tr("ملخص حركات الصندوق خلال فترة محددة")));
    root->addLayout(quick);
    root->addLayout(range);
    root->addWidget(summaryCard);
    root->addWidget(tableCard, 1);

    connect(todayButton, &QPushButton::clicked, this, &ReportsPage::onToday);
    connect(yesterdayButton, &QPushButton::clicked, this, &ReportsPage::onYesterday);
    connect(weekButton, &QPushButton::clicked, this, &ReportsPage::onThisWeek);
    connect(monthButton, &QPushButton::clicked, this, &ReportsPage::onThisMonth);
    connect(allButton, &QPushButton::clicked, this, &ReportsPage::onAll);
    connect(m_fromEdit, &QDateEdit::dateChanged, this, &ReportsPage::onRangeChanged);
    connect(m_toEdit, &QDateEdit::dateChanged, this, &ReportsPage::onRangeChanged);

    onToday();
}

void ReportsPage::onToday()
{
    const QDate today = QDate::currentDate();
    m_fromEdit->setDate(today);
    m_toEdit->setDate(today);
    fromTo(startOfDay(today), QDateTime::currentDateTime());
}

void ReportsPage::onYesterday()
{
    const QDate yesterday = QDate::currentDate().addDays(-1);
    m_fromEdit->setDate(yesterday);
    m_toEdit->setDate(yesterday);
    fromTo(startOfDay(yesterday), endOfDay(yesterday));
}

void ReportsPage::onThisWeek()
{
    const QDate today = QDate::currentDate();
    const QDate weekStart = today.addDays(-(today.dayOfWeek() - 1)); // Monday-first
    m_fromEdit->setDate(weekStart);
    m_toEdit->setDate(today);
    fromTo(startOfDay(weekStart), QDateTime::currentDateTime());
}

void ReportsPage::onThisMonth()
{
    const QDate today = QDate::currentDate();
    const QDate monthStart(today.year(), today.month(), 1);
    m_fromEdit->setDate(monthStart);
    m_toEdit->setDate(today);
    fromTo(startOfDay(monthStart), QDateTime::currentDateTime());
}

void ReportsPage::onAll()
{
    m_fromEdit->setDate(QDate(2000, 1, 1));
    m_toEdit->setDate(QDate::currentDate());
    fromTo(QDateTime(QDate(2000, 1, 1), QTime(0, 0, 0)), QDateTime::currentDateTime());
}

void ReportsPage::onRangeChanged()
{
    fromTo(startOfDay(m_fromEdit->date()), endOfDay(m_toEdit->date()));
}

void ReportsPage::fromTo(const QDateTime& from, const QDateTime& to)
{
    data::ReportService service(m_db);
    m_report = service.build(from, to);
    rebuild();
}

void ReportsPage::rebuild()
{
    m_summary->setText(tr("المبيعات: %1 عملية (الصافي: %2)  |  الربح الإجمالي: %3  |  صافي الربح: %4")
                           .arg(m_report.salesCount)
                           .arg(formatMoney(m_report.revenueCents))
                           .arg(formatMoney(m_report.grossProfitCents))
                           .arg(formatMoney(m_report.netProfitCents)));

    m_costs->setText(tr("تكلفة المبيعات: %1  |  المصاريف: %2  |  السحوبات: %3")
                         .arg(formatMoney(m_report.cogsCents))
                         .arg(formatMoney(m_report.expensesCents))
                         .arg(formatMoney(m_report.drawingsCents)));

    m_bottom->setText(tr("وعاء الزكاة: %1  |  الزكاة (2.5%): %2   —   جلسات في الفترة: %3 (رأس الفتح: %4)"
                                     "   —   ديون العملاء المستحقة اليوم: %5")
                          .arg(formatMoney(m_report.zakatBaseCents))
                          .arg(formatMoney(m_report.zakatCents))
                          .arg(m_report.sessionsOpened)
                          .arg(formatMoney(m_report.openingFloatCents))
                          .arg(formatMoney(m_report.outstandingDebtCents)));

    m_cashTable->setRowCount(0);
    for (const data::CashLine& line : m_report.cashLines) {
        const int row = m_cashTable->rowCount();
        m_cashTable->insertRow(row);
        m_cashTable->setItem(row, 0, new QTableWidgetItem(cashTypeLabel(line.type)));
        m_cashTable->setItem(row, 1, new QTableWidgetItem(QString::number(line.count)));
        m_cashTable->setItem(row, 2, new QTableWidgetItem(formatMoney(line.sumCents)));
    }
}

void ReportsPage::refresh()
{
    onRangeChanged();
}

} // namespace app::ui