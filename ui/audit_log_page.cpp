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
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

AuditLogPage::AuditLogPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_todayOnly = new QCheckBox(tr("اليوم فقط"));
    m_todayOnly->setChecked(true);
    m_todayOnly->setObjectName(QStringLiteral("secondary"));

    m_summary = new QLabel;
    m_summary->setObjectName(QStringLiteral("faintText"));
    m_summary->setMinimumHeight(38);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("auditTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(false);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("الوقت"), tr("المسؤول"), tr("العملية"), tr("التفاصيل")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(42);

    auto* top = new QHBoxLayout;
    top->setSpacing(10);
    top->addWidget(m_todayOnly);
    top->addStretch(1);

    auto* card = makeCard();
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 16, 18, 16);
    cardLayout->setSpacing(10);
    cardLayout->addLayout(top);
    cardLayout->addWidget(m_summary);
    cardLayout->addWidget(m_table, 1);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(tr("سجل التدقيق"),
                                   tr("كل العمليات الحساسة — تغييرات، أخطاء ومخالفات أسعار")));
    root->addWidget(card, 1);

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

    m_summary->setText(tr("عدد الأحداث: %1").arg(entries.size()));
}

int AuditLogPage::rowCount() const
{
    return m_table->rowCount();
}

} // namespace app::ui