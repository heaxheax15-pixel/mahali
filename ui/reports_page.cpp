#include "reports_page.h"

#include <QCoreApplication>
#include <QButtonGroup>
#include <QDate>
#include <QDateEdit>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <array>
#include <utility>

#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

#include "core/cash_movement.h"
#include "data/report_service.h"
#include "data/setting_repository.h"
#include "data/zakat_history_repository.h"

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

long long nisabCentsFor(data::Database& db)
{
    data::SettingRepository settings(db);
    return settings.value(QStringLiteral("nisab_cents")).value_or(QString()).toLongLong();
}

} // namespace

ReportsPage::ReportsPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* pageLayout = new QVBoxLayout(this);
    padPageLayout(pageLayout);

    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("reportsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget;
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    m_fromEdit = new DateField;
    m_fromEdit->setCalendarPopup(true);
    m_fromEdit->setMinimumSize(136, 40);
    m_toEdit = new DateField;
    m_toEdit->setCalendarPopup(true);
    m_toEdit->setMinimumSize(136, 40);

    auto* controls = new QHBoxLayout;
    controls->setContentsMargins(0, 0, 0, 0);
    controls->setSpacing(4);
    m_periodButtons = new QButtonGroup(this);
    m_periodButtons->setExclusive(true);
    const std::array<std::pair<QString, void (ReportsPage::*)()>, 5> periods = {{
        {tr("اليوم"), &ReportsPage::onToday},
        {tr("أمس"), &ReportsPage::onYesterday},
        {tr("هذا الأسبوع"), &ReportsPage::onThisWeek},
        {tr("هذا الشهر"), &ReportsPage::onThisMonth},
        {tr("الكل"), &ReportsPage::onAll},
    }};
    int commonButtonWidth = 0;
    for (const auto& period : periods) {
        auto* button = new QPushButton(period.first);
        button->setObjectName(QStringLiteral("periodButton"));
        button->setCheckable(true);
        button->setMinimumHeight(40);
        m_periodButtons->addButton(button);
        m_periodControls.append(button);
        commonButtonWidth = qMax(commonButtonWidth, button->sizeHint().width());
        connect(button, &QPushButton::clicked, this, period.second);
        controls->addWidget(button, 1);
    }
    for (QPushButton* button : m_periodControls) {
        button->setMinimumWidth(commonButtonWidth);
    }
    root->addLayout(controls);

    auto* dateControls = new QHBoxLayout;
    dateControls->setContentsMargins(0, 0, 0, 0);
    dateControls->setSpacing(8);
    dateControls->addWidget(new QLabel(tr("From:")));
    dateControls->addWidget(m_fromEdit, 1);
    dateControls->addWidget(new QLabel(tr("To:")));
    dateControls->addWidget(m_toEdit, 1);
    root->addLayout(dateControls);

    auto* metricGrid = new QGridLayout;
    metricGrid->setContentsMargins(0, 0, 0, 0);
    metricGrid->setSpacing(8);
    const auto addMetricCard = [this](QGridLayout* grid, int row, int column,
                                      const QString& title, const QString& detail = QString()) {
        auto* card = new QFrame;
        card->setObjectName(QStringLiteral("statCard"));
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(12, 8, 12, 8);
        layout->setSpacing(2);
        auto* caption = new QLabel(title);
        caption->setObjectName(QStringLiteral("metricCaption"));
        auto* value = new QLabel(QStringLiteral("0.00"));
        value->setObjectName(QStringLiteral("reportMetricValue"));
        value->setMinimumWidth(0);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(caption);
        layout->addWidget(value);
        if (!detail.isEmpty()) {
            m_salesDetail = new QLabel(detail);
            m_salesDetail->setObjectName(QStringLiteral("metricDetail"));
            layout->addWidget(m_salesDetail);
        }
        grid->addWidget(card, row, column);
        m_metricValues.append(value);
        return value;
    };

    auto* periodHeading = new QLabel(tr("Période sélectionnée"));
    periodHeading->setObjectName(QStringLiteral("reportSectionTitle"));
    root->addWidget(periodHeading);
    addMetricCard(metricGrid, 0, 0, tr("Ventes"), tr("Nombre d'opérations et net"));
    addMetricCard(metricGrid, 0, 1, tr("Bénéfice brut"));
    addMetricCard(metricGrid, 0, 2, tr("Bénéfice net"));
    addMetricCard(metricGrid, 1, 0, tr("Coût des ventes"));
    addMetricCard(metricGrid, 1, 1, tr("Dépenses"));
    addMetricCard(metricGrid, 1, 2, tr("Retraits"));
    for (int column = 0; column < 3; ++column) {
        metricGrid->setColumnStretch(column, 1);
    }
    root->addLayout(metricGrid);

    auto* currentHeading = new QLabel(tr("Zakat (état actuel, indépendant de la période)"));
    currentHeading->setObjectName(QStringLiteral("reportSectionTitle"));
    root->addWidget(currentHeading);
    auto* currentGrid = new QGridLayout;
    currentGrid->setContentsMargins(0, 0, 0, 0);
    currentGrid->setSpacing(8);
    addMetricCard(currentGrid, 0, 0, tr("Valeur du stock"));
    addMetricCard(currentGrid, 0, 1, tr("Trésorerie en caisse"));
    addMetricCard(currentGrid, 0, 2, tr("Créances clients"));
    addMetricCard(currentGrid, 1, 0, tr("Dettes fournisseurs"));
    addMetricCard(currentGrid, 1, 1, tr("Total imposable"));
    addMetricCard(currentGrid, 1, 2, tr("Nisab"));
    addMetricCard(currentGrid, 2, 0, tr("Zakat (2,5 %)"));
    addMetricCard(currentGrid, 2, 1, tr("Sessions sur la période"));
    addMetricCard(currentGrid, 2, 2, tr("Fonds de départ"));
    for (int column = 0; column < 3; ++column) {
        currentGrid->setColumnStretch(column, 1);
    }
    root->addLayout(currentGrid);

    // The per-year ledger, under the live figures above it. The 3x3 grid answers
    // "where does the shop stand right now" and resets itself on every refresh;
    // this is the part that does not reset, since a year that was assessed and
    // paid is not re-derivable from today's stock.
    auto* historyHeading = new QLabel(tr("Historique de la Zakat"));
    historyHeading->setObjectName(QStringLiteral("reportSectionTitle"));
    root->addWidget(historyHeading);
    m_zakatHistoryTable = new QTableWidget;
    m_zakatHistoryTable->setObjectName(QStringLiteral("reportTable"));
    m_zakatHistoryTable->setAlternatingRowColors(true);
    m_zakatHistoryTable->setFrameShape(QFrame::NoFrame);
    m_zakatHistoryTable->setShowGrid(true);
    m_zakatHistoryTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_zakatHistoryTable->setColumnCount(4);
    m_zakatHistoryTable->setHorizontalHeaderLabels(
        {tr("Année"), tr("Base"), tr("Dû"), tr("Statut")});
    m_zakatHistoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_zakatHistoryTable->setSelectionMode(QAbstractItemView::NoSelection);
    // The status column takes the slack so the two money columns keep the width
    // they need to show a figure instead of truncating it mid-number.
    m_zakatHistoryTable->horizontalHeader()->setStretchLastSection(false);
    m_zakatHistoryTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_zakatHistoryTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_zakatHistoryTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_zakatHistoryTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_zakatHistoryTable->setColumnWidth(0, 80);
    m_zakatHistoryTable->setColumnWidth(1, 140);
    m_zakatHistoryTable->setColumnWidth(2, 140);
    m_zakatHistoryTable->horizontalHeader()->setFixedHeight(34);
    m_zakatHistoryTable->verticalHeader()->setDefaultSectionSize(36);
    m_zakatHistoryTable->verticalHeader()->hide();
    // Bounded: one row a year, and a decade of history does not need the same
    // height as the operations table it shares the page with. The scroll area
    // handles the overflow.
    m_zakatHistoryTable->setMaximumHeight(240);
    m_zakatHistoryTable->setMinimumHeight(120);

    auto* historyCard = makeCard();
    auto* historyLayout = new QVBoxLayout(historyCard);
    padCardLayout(historyLayout);
    m_zakatHistoryStack = new QStackedWidget;
    m_zakatHistoryStack->addWidget(m_zakatHistoryTable);
    // An empty history is a normal state, not a broken table: say so rather than
    // showing a header with nothing under it.
    auto* emptyHistory = new QLabel(tr("Aucun exercice de Zakat enregistré"));
    emptyHistory->setObjectName(QStringLiteral("emptyReportState"));
    emptyHistory->setAlignment(Qt::AlignCenter);
    m_zakatHistoryStack->addWidget(emptyHistory);
    historyLayout->addWidget(m_zakatHistoryStack);
    root->addWidget(historyCard);

    m_cashTable = new QTableWidget;
    m_cashTable->setObjectName(QStringLiteral("reportTable"));
    m_cashTable->setAlternatingRowColors(true);
    m_cashTable->setFrameShape(QFrame::NoFrame);
    m_cashTable->setShowGrid(true);
    m_cashTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_cashTable->setColumnCount(3);
    m_cashTable->setHorizontalHeaderLabels({tr("العملية"), tr("العدد"), tr("المجموع")});
    m_cashTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_cashTable->horizontalHeader()->setStretchLastSection(false);
    m_cashTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_cashTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_cashTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_cashTable->setColumnWidth(1, 100);
    m_cashTable->setColumnWidth(2, 150);
    m_cashTable->horizontalHeader()->setFixedHeight(34);
    m_cashTable->verticalHeader()->setDefaultSectionSize(36);
    m_cashTable->verticalHeader()->hide();

    auto* tableCard = makeCard();
    auto* tableLayout = new QVBoxLayout(tableCard);
    padCardLayout(tableLayout);
    tableLayout->addWidget(makeCardTitle(tr("تفاصيل العمليات")));
    m_tableStack = new QStackedWidget;
    m_cashTable->setMinimumHeight(280);
    m_tableStack->addWidget(m_cashTable);
    auto* emptyTable = new QLabel(tr("Aucune opération sur cette période"));
    emptyTable->setObjectName(QStringLiteral("emptyReportState"));
    emptyTable->setAlignment(Qt::AlignCenter);
    m_tableStack->addWidget(emptyTable);
    tableLayout->addWidget(m_tableStack, 1);
    root->addWidget(tableCard, 1);

    scroll->setWidget(content);
    pageLayout->addWidget(scroll);
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
    m_periodControls[0]->setChecked(true);
}

void ReportsPage::onYesterday()
{
    const QDate yesterday = QDate::currentDate().addDays(-1);
    m_fromEdit->setDate(yesterday);
    m_toEdit->setDate(yesterday);
    fromTo(startOfDay(yesterday), endOfDay(yesterday));
    m_periodControls[1]->setChecked(true);
}

void ReportsPage::onThisWeek()
{
    const QDate today = QDate::currentDate();
    const QDate weekStart = today.addDays(-(today.dayOfWeek() - 1)); // Monday-first
    m_fromEdit->setDate(weekStart);
    m_toEdit->setDate(today);
    fromTo(startOfDay(weekStart), QDateTime::currentDateTime());
    m_periodControls[2]->setChecked(true);
}

void ReportsPage::onThisMonth()
{
    const QDate today = QDate::currentDate();
    const QDate monthStart(today.year(), today.month(), 1);
    m_fromEdit->setDate(monthStart);
    m_toEdit->setDate(today);
    fromTo(startOfDay(monthStart), QDateTime::currentDateTime());
    m_periodControls[3]->setChecked(true);
}

void ReportsPage::onAll()
{
    m_fromEdit->setDate(QDate(2000, 1, 1));
    m_toEdit->setDate(QDate::currentDate());
    fromTo(QDateTime(QDate(2000, 1, 1), QTime(0, 0, 0)), QDateTime::currentDateTime());
    m_periodControls[4]->setChecked(true);
}

void ReportsPage::onRangeChanged()
{
    for (QPushButton* button : m_periodControls) {
        button->setChecked(false);
    }
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
    m_cashTable->setRowCount(0);
    const auto setValue = [this](int index, const QString& text, long long cents,
                                 bool financialValue = true) {
        QLabel* label = m_metricValues[index];
        label->setText(text);
        label->setProperty("deltaState", !financialValue
                                               ? QStringLiteral("neutral")
                                               : cents < 0 ? QStringLiteral("negative")
                                                           : cents > 0 ? QStringLiteral("positive")
                                                                       : QStringLiteral("neutral"));
        label->style()->unpolish(label);
        label->style()->polish(label);
    };
    setValue(0, QString::number(m_report.salesCount), m_report.revenueCents);
    m_salesDetail->setText(tr("Net : %1").arg(formatMoney(m_report.revenueCents)));
    setValue(1, formatMoney(m_report.grossProfitCents), m_report.grossProfitCents);
    setValue(2, formatMoney(m_report.netProfitCents), m_report.netProfitCents);
    setValue(3, formatMoney(m_report.cogsCents), m_report.cogsCents);
    setValue(4, formatMoney(m_report.expensesCents), m_report.expensesCents);
    setValue(5, formatMoney(m_report.drawingsCents), m_report.drawingsCents);
    setValue(6, formatMoney(m_report.stockValueCents), m_report.stockValueCents);
    setValue(7, formatMoney(m_report.cashOnHandCents), m_report.cashOnHandCents);
    setValue(8, formatMoney(m_report.receivablesCents), m_report.receivablesCents);
    setValue(9, formatMoney(m_report.supplierDebtCents), m_report.supplierDebtCents);
    setValue(10, formatMoney(m_report.zakatBaseCents), m_report.zakatBaseCents);
    const long long nisab = nisabCentsFor(m_db);
    setValue(11, formatMoney(nisab), 0, false);
    setValue(12, formatMoney(m_report.zakatCents), m_report.zakatCents);
    setValue(13, QString::number(m_report.sessionsOpened), 0, false);
    setValue(14, formatMoney(m_report.openingFloatCents), m_report.openingFloatCents);

    long long totalCount = 0;
    long long totalCents = 0;
    for (const data::CashLine& line : m_report.cashLines) {
        const int row = m_cashTable->rowCount();
        m_cashTable->insertRow(row);
        m_cashTable->setItem(row, 0, new QTableWidgetItem(cashTypeLabel(line.type)));
        auto* count = new QTableWidgetItem(QString::number(line.count));
        count->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_cashTable->setItem(row, 1, count);
        auto* sum = new QTableWidgetItem(formatMoney(line.sumCents));
        sum->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_cashTable->setItem(row, 2, sum);
        totalCount += line.count;
        totalCents += line.sumCents;
    }
    // Read fresh rather than from m_report: the rows are years, not this period's
    // figures, so they must not follow the date filters. The panel refreshes on
    // every rebuild only because that is the one call that runs, and the read is
    // a handful of rows — cheap enough not to want a second trigger to keep in
    // step with.
    rebuildZakatHistory();
    if (m_cashTable->rowCount() == 0) {
        m_tableStack->setCurrentIndex(1);
    } else {
        m_tableStack->setCurrentIndex(0);
        const int totalRow = m_cashTable->rowCount();
        m_cashTable->insertRow(totalRow);
        auto* totalLabel = new QTableWidgetItem(tr("Total"));
        auto* totalCountItem = new QTableWidgetItem(QString::number(totalCount));
        auto* totalSumItem = new QTableWidgetItem(formatMoney(totalCents));
        for (QTableWidgetItem* item : {totalLabel, totalCountItem, totalSumItem}) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
        totalCountItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        totalSumItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_cashTable->setItem(totalRow, 0, totalLabel);
        m_cashTable->setItem(totalRow, 1, totalCountItem);
        m_cashTable->setItem(totalRow, 2, totalSumItem);
    }
}

void ReportsPage::rebuildZakatHistory()
{
    const std::vector<data::ZakatHistory> rows = data::ZakatHistoryRepository(m_db).findAll();

    m_zakatHistoryTable->setRowCount(0);
    for (const data::ZakatHistory& history : rows) {
        const int row = m_zakatHistoryTable->rowCount();
        m_zakatHistoryTable->insertRow(row);

        auto* year = new QTableWidgetItem(QString::number(history.year));
        year->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_zakatHistoryTable->setItem(row, 0, year);

        auto* base = new QTableWidgetItem(formatMoney(history.baseCents));
        base->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_zakatHistoryTable->setItem(row, 1, base);

        auto* due = new QTableWidgetItem(formatMoney(history.dueCents));
        due->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_zakatHistoryTable->setItem(row, 2, due);

        // Paid says it was settled, so what goes in the cell is when. An unpaid
        // year shows only that: there is no date to show and inventing one would
        // read as though something had been recorded on the day the page opened.
        m_zakatHistoryTable->setItem(row, 3, new QTableWidgetItem(
            history.paid ? tr("Payée le %1").arg(history.paidAt) : tr("Non payée")));
    }

    if (m_zakatHistoryTable->rowCount() == 0) {
        m_zakatHistoryStack->setCurrentIndex(1);
    } else {
        m_zakatHistoryStack->setCurrentIndex(0);
    }
}

void ReportsPage::refresh()
{
    onRangeChanged();
}

} // namespace app::ui