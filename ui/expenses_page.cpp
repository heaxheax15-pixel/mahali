#include "expenses_page.h"

#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/audit_log_entry.h"
#include "core/session.h"
#include "data/audit_log_repository.h"
#include "data/cash_entry_service.h"
#include "data/cash_session_repository.h"
#include "data/expense_repository.h"
#include "data/owner_drawing_repository.h"
#include "format_utils.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/stat_card.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

// Stable type tokens kept in the type cell's Qt::UserRole. These are compared, never
// displayed: reading the visible label instead would break the moment it is translated.
const QString kExpenseType = QStringLiteral("expense");
const QString kDrawingType = QStringLiteral("drawing");

void writeAudit(app::data::Database& db, const QString& action, const QString& target)
{
    core::AuditLogEntry entry;
    entry.actor = app::core::Session::instance().actorName();
    entry.action = action;
    entry.target = target;
    entry.createdAt = QDateTime::currentDateTime();
    data::AuditLogRepository(db).insert(entry);
}

} // namespace

ExpensesPage::ExpensesPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    auto* header = new PageHeader(tr("المصروفات"),
                                  tr("المصاريف وسحوبات المالك خارجة من الصندوق"));
    m_expensesCard = new StatCard(tr("مصاريف اليوم"));
    m_expensesCard->setIcon(Icon::Receipt, QStringLiteral("#c8860f"));
    m_drawingsCard = new StatCard(tr("سحوبات اليوم"));
    m_drawingsCard->setIcon(Icon::Wallet, QStringLiteral("#c84444"));

    auto* cards = new QHBoxLayout;
    cards->setSpacing(10);
    cards->addWidget(m_expensesCard, 1);
    cards->addWidget(m_drawingsCard, 1);

    m_expenseButton = new QPushButton(tr("مصروف جديد"));
    m_expenseButton->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    m_drawingButton = new QPushButton(tr("سحب مالك"));
    m_drawingButton->setObjectName(QStringLiteral("secondary"));
    m_reverseButton = new QPushButton(tr("عكس المحدد"));
    m_reverseButton->setObjectName(QStringLiteral("secondary"));
    m_reverseButton->setEnabled(false);

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(m_expenseButton);
    toolbar->addWidget(m_drawingButton);
    toolbar->addWidget(m_reverseButton);
    toolbar->addStretch(1);

    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("infoBar"));
    m_summary->setMinimumHeight(46);

    m_notice = new QLabel;
    m_notice->setWordWrap(true);
    m_notice->setMinimumHeight(42);
    m_notice->setObjectName(QStringLiteral("noticeOk"));

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("expenseTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(false);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("الوقت"), tr("النوع"), tr("المبلغ"), tr("بيان")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setDefaultSectionSize(42);

    auto* tableCard = makeCard();
    auto* tableLayout = new QVBoxLayout(tableCard);
    tableLayout->setContentsMargins(18, 16, 18, 16);
    tableLayout->setSpacing(10);
    tableLayout->addWidget(makeCardTitle(tr("سجل مصاريف وسحوبات اليوم")));
    tableLayout->addLayout(toolbar);
    tableLayout->addWidget(m_table, 1);
    tableLayout->addWidget(m_summary);
    tableLayout->addWidget(m_notice);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(header);
    root->addLayout(cards);
    root->addWidget(tableCard, 1);

    connect(m_expenseButton, &QPushButton::clicked, this, &ExpensesPage::onExpenseClicked);
    connect(m_drawingButton, &QPushButton::clicked, this, &ExpensesPage::onDrawingClicked);
    connect(m_reverseButton, &QPushButton::clicked, this, &ExpensesPage::onReverseClicked);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this]() { m_reverseButton->setEnabled(m_table->currentRow() >= 0); });

    refresh();
}

void ExpensesPage::refresh()
{
    data::ExpenseRepository expenses(m_db);
    data::OwnerDrawingRepository drawings(m_db);
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime dayStart = now;
    dayStart.setTime(QTime(0, 0, 0));

    m_table->setRowCount(0);
    long long expenseTotal = 0;
    long long drawingTotal = 0;
    const auto expenseRows = expenses.findBetween(dayStart, now);
    for (const core::Expense& expense : expenseRows) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(expense.createdAt.toString(QStringLiteral("HH:mm"))));
        m_table->setItem(row, 1, new QTableWidgetItem(tr("مصروف")));
        m_table->item(row, 1)->setData(Qt::UserRole, kExpenseType);
        m_table->setItem(row, 2, new QTableWidgetItem(formatMoney(expense.amountCents)));
        m_table->setItem(row, 3, new QTableWidgetItem(expense.label));
        m_table->item(row, 0)->setData(Qt::UserRole, expense.id);
        expenseTotal += expense.amountCents;
    }
    const auto drawingRows = drawings.findBetween(dayStart, now);
    for (const core::OwnerDrawing& drawing : drawingRows) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(drawing.createdAt.toString(QStringLiteral("HH:mm"))));
        m_table->setItem(row, 1, new QTableWidgetItem(tr("سحب")));
        m_table->item(row, 1)->setData(Qt::UserRole, kDrawingType);
        m_table->setItem(row, 2, new QTableWidgetItem(formatMoney(drawing.amountCents)));
        m_table->setItem(row, 3, new QTableWidgetItem(drawing.note));
        m_table->item(row, 0)->setData(Qt::UserRole, drawing.id);
        drawingTotal += drawing.amountCents;
    }

    m_expensesCard->setCents(expenseTotal);
    m_drawingsCard->setCents(drawingTotal);

    m_summary->setText(tr("مصاريف اليوم: %1  |  سحوبات اليوم: %2")
                           .arg(formatMoney(expenseTotal))
                           .arg(formatMoney(drawingTotal)));
}

int ExpensesPage::entryCount() const
{
    return m_table->rowCount();
}

long long ExpensesPage::expensesTotalCents() const
{
    long long total = 0;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (m_table->item(i, 1)->data(Qt::UserRole).toString() == kExpenseType) {
            total += parseMoney(m_table->item(i, 2)->text()).value_or(0);
        }
    }
    return total;
}

QString ExpensesPage::noticeText() const
{
    return m_notice->text();
}

void ExpensesPage::onExpenseClicked()
{
    bool ok = false;
    const QString label =
        QInputDialog::getText(this, tr("مصروف جديد"), tr("البيان (مثل: كهرباء):"),
                              QLineEdit::Normal, QString(), &ok);
    if (!ok || label.trimmed().isEmpty()) {
        return;
    }
    const QString amount = QInputDialog::getText(this, tr("مصروف جديد"),
                                                 tr("المبلغ:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return;
    }
    const auto cents = parseMoney(amount);
    if (!cents || *cents <= 0) {
        QMessageBox::warning(this, tr("خطأ"), tr("المبلغ غير صالح"));
        return;
    }
    recordExpense(label.trimmed(), *cents);
}

void ExpensesPage::onDrawingClicked()
{
    bool ok = false;
    const QString note =
        QInputDialog::getText(this, tr("سحب مالك"), tr("ملاحظة (اختياري):"), QLineEdit::Normal,
                              QString(), &ok);
    if (!ok) {
        return;
    }
    const QString amount =
        QInputDialog::getText(this, tr("سحب مالك"), tr("المبلغ:"), QLineEdit::Normal,
                              QString(), &ok);
    if (!ok) {
        return;
    }
    const auto cents = parseMoney(amount);
    if (!cents || *cents <= 0) {
        QMessageBox::warning(this, tr("خطأ"), tr("المبلغ غير صالح"));
        return;
    }
    recordDrawing(note.trimmed(), *cents);
}

void ExpensesPage::recordExpense(const QString& label, long long amountCents)
{
    m_notice->clear();
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(tr("لا توجد جلسة مفتوحة — افتح جلسة الصندوق أولاً"));
        return;
    }
    data::CashEntryService service(m_db);
    const data::CashEntryResult result = service.recordExpense(label, amountCents, session->id);
    if (!result.ok) {
        m_notice->setText(tr("تعذر تسجيل المصروف: %1").arg(result.error));
        return;
    }
    m_notice->setText(tr("سُجّل مصروف: %1 — %2").arg(formatMoney(result.amountCents), label));
    refresh();
}

void ExpensesPage::recordDrawing(const QString& note, long long amountCents)
{
    m_notice->clear();
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(tr("لا توجد جلسة مفتوحة — افتح جلسة الصندوق أولاً"));
        return;
    }
    data::CashEntryService service(m_db);
    const data::CashEntryResult result = service.recordDrawing(note, amountCents, session->id);
    if (!result.ok) {
        m_notice->setText(tr("تعذر تسجيل السحب: %1").arg(result.error));
        return;
    }
    m_notice->setText(tr("سُجّل سحب: %1").arg(formatMoney(result.amountCents)));
    refresh();
}

void ExpensesPage::reverseRow(int row)
{
    m_notice->clear();
    if (row < 0 || row >= m_table->rowCount()) {
        return;
    }
    const int entryId = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    const QString typeToken = m_table->item(row, 1)->data(Qt::UserRole).toString();
    const QString typeLabel = m_table->item(row, 1)->text();
    if (entryId <= 0) {
        return;
    }

    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(tr("لا توجد جلسة مفتوحة — افتح جلسة الصندوق أولاً"));
        return;
    }

    data::CashEntryService service(m_db);
    const data::CashEntryResult result =
        typeToken == kExpenseType ? service.reverseExpense(entryId, session->id)
                                  : service.reverseDrawing(entryId, session->id);
    if (!result.ok) {
        m_notice->setText(tr("تعذر العكس: %1").arg(result.error));
        return;
    }
    writeAudit(m_db, QStringLiteral("entry_reversal"),
               QStringLiteral("%1 #%2 (%3)")
                   .arg(typeLabel)
                   .arg(entryId)
                   .arg(formatMoney(result.amountCents)));
    m_notice->setText(tr("أُلغي: %1").arg(formatMoney(result.amountCents)));
    refresh();
}

void ExpensesPage::onReverseClicked()
{
    reverseRow(m_table->currentRow());
}

} // namespace app::ui