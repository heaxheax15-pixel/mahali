#include "audit_log_page.h"

#include <QCheckBox>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QTime>
#include <QVBoxLayout>

#include <algorithm>

#include "data/audit_log_repository.h"

namespace app::ui {

AuditLogPage::AuditLogPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_todayOnly = new QCheckBox(QStringLiteral("اليوم فقط"));
    m_todayOnly->setChecked(true);

    m_summary = new QLabel;

    m_table = new QTableWidget;
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("الوقت"), QStringLiteral("المسؤول"), QStringLiteral("العملية"), QStringLiteral("التفاصيل")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);

    auto* top = new QHBoxLayout;
    top->addWidget(m_todayOnly);
    top->addStretch(1);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_summary);
    layout->addWidget(m_table);

    connect(m_todayOnly, &QCheckBox::toggled, this, &AuditLogPage::onTodayToggled);

    refresh();
}

void AuditLogPage::onTodayToggled()
{
    refresh();
}

void AuditLogPage::refresh()
{
    data::AuditLogRepository audit(m_db);

    std::vector<core::AuditLogEntry> entries;
    if (m_todayOnly->isChecked()) {
        QDateTime dayStart = QDateTime::currentDateTime();
        dayStart.setTime(QTime(0, 0, 0));
        const auto rows = audit.findBetween(dayStart, QDateTime::currentDateTime());
        entries.assign(rows.begin(), rows.end());
    } else {
        const auto rows = audit.findAll();
        entries.assign(rows.begin(), rows.end());
    }
    std::sort(entries.begin(), entries.end(),
              [](const core::AuditLogEntry& a, const core::AuditLogEntry& b) { return a.createdAt > b.createdAt; });

    m_table->setRowCount(0);
    for (const core::AuditLogEntry& entry : entries) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(entry.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        m_table->setItem(row, 1, new QTableWidgetItem(entry.actor));
        m_table->setItem(row, 2, new QTableWidgetItem(entry.action));
        m_table->setItem(row, 3, new QTableWidgetItem(entry.target));
    }

    m_summary->setText(QStringLiteral("عدد الأحداث: %1").arg(entries.size()));
}

int AuditLogPage::rowCount() const
{
    return m_table->rowCount();
}

} // namespace app::ui