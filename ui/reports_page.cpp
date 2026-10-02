#include "reports_page.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateEdit>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

#include "core/cash_movement.h"
#include "data/report_service.h"
#include "format_utils.h"
#include "theme.h"

namespace app::ui {

namespace {

QString cashTypeLabel(const QString& type)
{
    if (type == core::cashMovementType::kSale) {
        return QCoreApplication::translate("app::ui::ReportsPage", "بيع");
    }
    if (type == core::cashMovementType::kCustomerPayment) {
        return QCoreApplication::translate("app::ui::ReportsPage", "دفع عميل");
    }
    if (type == core::cashMovementType::kExpense) {
        return QCoreApplication::translate("app::ui::ReportsPage", "مصروف");
    }
    if (type == core::cashMovementType::kDrawing) {
        return QCoreApplication::translate("app::ui::ReportsPage", "سحب مالك");
    }
    if (type == core::cashMovementType::kSupplierPayment) {
        return QCoreApplication::translate("app::ui::ReportsPage", "سداد مورد");
    }
    if (type == core::cashMovementType::kRefund) {
        return QCoreApplication::translate("app::ui::ReportsPage", "استرداد");
    }
    return type;
}

/* A QDateEdit with a calendar popup is a spin box, and the two controls it
   actually draws are the up and down buttons, not the drop-down arrow a
   stylesheet rule for a combo box would reach. Left alone they keep the
   platform's own frame and glyph, which is what read as a dash at the trailing
   edge of the field. The stylesheet now collapses both to nothing, and the
   single triangle that replaces them is painted here: a stylesheet can put an
   image into a subcontrol but cannot assemble a shape out of borders, so the
   glyph has to come from a painter. Deriving from QDateEdit keeps the members
   in the header untouched. */
class DateField : public QDateEdit
{
public:
    using QDateEdit::QDateEdit;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QDateEdit::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(activeTheme() == QStringLiteral("dark")
                             ? QColor(QStringLiteral("#a3a3a3"))
                             : QColor(QStringLiteral("#64748b")));

        // The trailing edge, clear of the text: the field's own horizontal
        // padding is 21px, so this sits inside it and follows the layout
        // direction without a second rule for the mirrored case.
        const QPointF tip(width() - 21.0, height() / 2.0);
        QPolygonF triangle;
        triangle << QPointF(tip.x() - 5.0, tip.y() - 3.0) << QPointF(tip.x() + 5.0, tip.y() - 3.0)
                 << QPointF(tip.x(), tip.y() + 4.0);
        painter.drawPolygon(triangle);
    }
};

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
    m_fromEdit = new DateField;
    m_fromEdit->setCalendarPopup(true);
    m_fromEdit->setMinimumHeight(42);
    m_toEdit = new DateField;
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
    m_summary->setVisible(false);
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
    m_cashTable->setShowGrid(true);
    m_cashTable->setColumnCount(3);
    m_cashTable->setHorizontalHeaderLabels(
        {tr("العملية"), tr("العدد"), tr("المجموع")});
    m_cashTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // The two narrow columns are pinned and the money takes the rest. A sum is
    // the only figure here with no natural length -- it is a run of digits with a
    // currency mark and separators -- so it is the one that has to be allowed to
    // grow with the window rather than be given a fixed width and then clipped.
    m_cashTable->horizontalHeader()->setStretchLastSection(false);
    m_cashTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_cashTable->setColumnWidth(0, 200);
    m_cashTable->setColumnWidth(1, 100);
    m_cashTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_cashTable->verticalHeader()->setDefaultSectionSize(42);
    m_cashTable->verticalHeader()->hide();

    // The three report lines as bare text, not a card. A card around them cost
    // 32px of padding, a 21px rounded border and a "Résumé" heading that said
    // nothing the lines under it did not: about 90px of the page, which on a
    // laptop screen is two rows of the table that has to be read instead. The
    // summary is reference, the table is the work.
    auto* summary = new QVBoxLayout;
    summary->setContentsMargins(4, 0, 4, 0);
    summary->setSpacing(6);
    summary->addWidget(m_summary);
    summary->addWidget(m_costs);
    summary->addWidget(m_bottom);

    auto* tableCard = makeCard();
    auto* tableLayout = new QVBoxLayout(tableCard);
    tableLayout->setContentsMargins(18, 16, 18, 16);
    tableLayout->setSpacing(10);
    tableLayout->addWidget(makeCardTitle(tr("تفاصيل العمليات")));
    tableLayout->addWidget(m_cashTable, 1);

    // The filters and the range share one row. They were two rows because the
    // summary card stood between nothing and everything, and two rows of
    // 42px controls is 42px the table does not get. The stretch is what keeps
    // the group against the leading edge in both layout directions.
    auto* controls = new QHBoxLayout;
    controls->setSpacing(8);
    for (QPushButton* button : {todayButton, yesterdayButton, weekButton, monthButton, allButton}) {
        controls->addWidget(button);
    }
    controls->addWidget(new QLabel(tr("من:")));
    controls->addWidget(m_fromEdit);
    controls->addWidget(new QLabel(tr("إلى:")));
    controls->addWidget(m_toEdit);
    controls->addStretch(1);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    // Title only. The subtitle named what the page is, and the summary lines
    // directly under it are exactly that: three lines of figures about the cash
    // movements over the selected period. Saying it above them spent a line on
    // a sentence the numbers then repeated.
    root->addWidget(new PageHeader(tr("التقارير"), QString()));
    root->addLayout(controls);
    root->addLayout(summary);
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
    m_summary->setVisible(!m_summary->text().isEmpty());

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