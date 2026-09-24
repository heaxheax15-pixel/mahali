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

#include "data/cash_entry_service.h"
#include "data/cash_session_repository.h"
#include "data/expense_repository.h"
#include "data/owner_drawing_repository.h"
#include "format_utils.h"

namespace app::ui {

ExpensesPage::ExpensesPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_expenseButton = new QPushButton(QStringLiteral("مصروف جديد"));
    m_drawingButton = new QPushButton(QStringLiteral("سحب مالك"));

    m_table = new QTableWidget;
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("الوقت"), QStringLiteral("النوع"), QStringLiteral("المبلغ"), QStringLiteral("بيان")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);

    m_summary = new QLabel;
    m_summary->setStyleSheet(QStringLiteral("font-weight: bold;"));
    m_notice = new QLabel;
    m_notice->setWordWrap(true);

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(m_expenseButton);
    toolbar->addWidget(m_drawingButton);
    toolbar->addStretch(1);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(toolbar);
    layout->addWidget(m_summary);
    layout->addWidget(m_table);
    layout->addWidget(m_notice);

    connect(m_expenseButton, &QPushButton::clicked, this, &ExpensesPage::onExpenseClicked);
    connect(m_drawingButton, &QPushButton::clicked, this, &ExpensesPage::onDrawingClicked);

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
        m_table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("مصروف")));
        m_table->setItem(row, 2, new QTableWidgetItem(formatMoney(expense.amountCents)));
        m_table->setItem(row, 3, new QTableWidgetItem(expense.label));
        expenseTotal += expense.amountCents;
    }
    const auto drawingRows = drawings.findBetween(dayStart, now);
    for (const core::OwnerDrawing& drawing : drawingRows) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(drawing.createdAt.toString(QStringLiteral("HH:mm"))));
        m_table->setItem(row, 1, new QTableWidgetItem(QStringLiteral("سحب")));
        m_table->setItem(row, 2, new QTableWidgetItem(formatMoney(drawing.amountCents)));
        m_table->setItem(row, 3, new QTableWidgetItem(drawing.note));
        drawingTotal += drawing.amountCents;
    }

    m_summary->setText(QStringLiteral("مصاريف اليوم: %1  |  سحوبات اليوم: %2")
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
        if (m_table->item(i, 1)->text() == QStringLiteral("مصروف")) {
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
        QInputDialog::getText(this, QStringLiteral("مصروف جديد"), QStringLiteral("البيان (مثل: كهرباء):"),
                              QLineEdit::Normal, QString(), &ok);
    if (!ok || label.trimmed().isEmpty()) {
        return;
    }
    const QString amount = QInputDialog::getText(this, QStringLiteral("مصروف جديد"),
                                                 QStringLiteral("المبلغ:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return;
    }
    const auto cents = parseMoney(amount);
    if (!cents || *cents <= 0) {
        QMessageBox::warning(this, QStringLiteral("خطأ"), QStringLiteral("المبلغ غير صالح"));
        return;
    }
    recordExpense(label.trimmed(), *cents);
}

void ExpensesPage::onDrawingClicked()
{
    bool ok = false;
    const QString note =
        QInputDialog::getText(this, QStringLiteral("سحب مالك"), QStringLiteral("ملاحظة (اختياري):"), QLineEdit::Normal,
                              QString(), &ok);
    if (!ok) {
        return;
    }
    const QString amount =
        QInputDialog::getText(this, QStringLiteral("سحب مالك"), QStringLiteral("المبلغ:"), QLineEdit::Normal,
                              QString(), &ok);
    if (!ok) {
        return;
    }
    const auto cents = parseMoney(amount);
    if (!cents || *cents <= 0) {
        QMessageBox::warning(this, QStringLiteral("خطأ"), QStringLiteral("المبلغ غير صالح"));
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
        m_notice->setText(QStringLiteral("لا توجد جلسة مفتوحة — افتح جلسة الصندوق أولاً"));
        return;
    }
    data::CashEntryService service(m_db);
    const data::CashEntryResult result = service.recordExpense(label, amountCents, session->id);
    if (!result.ok) {
        m_notice->setText(QStringLiteral("تعذر تسجيل المصروف: %1").arg(result.error));
        return;
    }
    m_notice->setText(QStringLiteral("سُجّل مصروف: %1 — %2").arg(formatMoney(result.amountCents), label));
    refresh();
}

void ExpensesPage::recordDrawing(const QString& note, long long amountCents)
{
    m_notice->clear();
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(QStringLiteral("لا توجد جلسة مفتوحة — افتح جلسة الصندوق أولاً"));
        return;
    }
    data::CashEntryService service(m_db);
    const data::CashEntryResult result = service.recordDrawing(note, amountCents, session->id);
    if (!result.ok) {
        m_notice->setText(QStringLiteral("تعذر تسجيل السحب: %1").arg(result.error));
        return;
    }
    m_notice->setText(QStringLiteral("سُجّل سحب: %1").arg(formatMoney(result.amountCents)));
    refresh();
}

} // namespace app::ui