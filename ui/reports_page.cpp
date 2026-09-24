#include "reports_page.h"

#include <QDate>
#include <QDateEdit>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "data/report_service.h"
#include "format_utils.h"

namespace app::ui {

namespace {

QString cashTypeLabel(const QString& type)
{
    if (type == QLatin1String("sale")) {
        return QStringLiteral("بيع");
    }
    if (type == QLatin1String("customer_payment")) {
        return QStringLiteral("دفع عميل");
    }
    if (type == QLatin1String("expense")) {
        return QStringLiteral("مصروف");
    }
    if (type == QLatin1String("drawing")) {
        return QStringLiteral("سحب مالك");
    }
    if (type == QLatin1String("refund")) {
        return QStringLiteral("استرداد");
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
    m_toEdit = new QDateEdit;
    m_toEdit->setCalendarPopup(true);

    auto* todayButton = new QPushButton(QStringLiteral("اليوم"));
    auto* yesterdayButton = new QPushButton(QStringLiteral("أمس"));
    auto* weekButton = new QPushButton(QStringLiteral("هذا الأسبوع"));
    auto* monthButton = new QPushButton(QStringLiteral("هذا الشهر"));
    auto* allButton = new QPushButton(QStringLiteral("الكل"));

    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    m_summary->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    m_costs = new QLabel;
    m_costs->setWordWrap(true);
    m_bottom = new QLabel;
    m_bottom->setWordWrap(true);

    m_cashTable = new QTableWidget;
    m_cashTable->setColumnCount(3);
    m_cashTable->setHorizontalHeaderLabels(
        {QStringLiteral("العملية"), QStringLiteral("العدد"), QStringLiteral("المجموع")});
    m_cashTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_cashTable->horizontalHeader()->setStretchLastSection(true);

    auto* quick = new QHBoxLayout;
    for (QPushButton* button : {todayButton, yesterdayButton, weekButton, monthButton, allButton}) {
        quick->addWidget(button);
    }
    quick->addStretch(1);

    auto* range = new QHBoxLayout;
    range->addWidget(new QLabel(QStringLiteral("من:")));
    range->addWidget(m_fromEdit);
    range->addWidget(new QLabel(QStringLiteral("إلى:")));
    range->addWidget(m_toEdit);
    range->addStretch(1);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(quick);
    layout->addLayout(range);
    layout->addWidget(m_summary);
    layout->addWidget(m_costs);
    layout->addWidget(m_bottom);
    layout->addWidget(m_cashTable);

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
    m_summary->setText(QStringLiteral("المبيعات: %1 عملية (الصافي: %2)  |  الربح الإجمالي: %3  |  صافي الربح: %4")
                           .arg(m_report.salesCount)
                           .arg(formatMoney(m_report.revenueCents))
                           .arg(formatMoney(m_report.grossProfitCents))
                           .arg(formatMoney(m_report.netProfitCents)));

    m_costs->setText(QStringLiteral("تكلفة المبيعات: %1  |  المصاريف: %2  |  السحوبات: %3")
                         .arg(formatMoney(m_report.cogsCents))
                         .arg(formatMoney(m_report.expensesCents))
                         .arg(formatMoney(m_report.drawingsCents)));

    m_bottom->setText(QStringLiteral("وعاء الزكاة: %1  |  الزكاة (2.5%): %2   —   جلسات في الفترة: %3 (رأس الفتح: %4)"
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