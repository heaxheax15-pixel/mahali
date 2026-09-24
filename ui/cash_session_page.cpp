#include "cash_session_page.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/cash_session_calculator.h"
#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "format_utils.h"

namespace app::ui {

namespace {

QString movementTypeLabel(const QString& type)
{
    if (type == QLatin1String("sale")) {
        return QStringLiteral("بيع");
    }
    if (type == QLatin1String("customer_payment")) {
        return QStringLiteral("دفعة عميل");
    }
    if (type == QLatin1String("expense")) {
        return QStringLiteral("مصروف");
    }
    if (type == QLatin1String("drawing")) {
        return QStringLiteral("سحب");
    }
    if (type == QLatin1String("refund")) {
        return QStringLiteral("استرداد");
    }
    return type;
}

} // namespace

CashSessionPage::CashSessionPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_openButton = new QPushButton(QStringLiteral("ابدأ جلسة"));
    m_closeButton = new QPushButton(QStringLiteral("أغلق الجلسة"));
    m_closeButton->setEnabled(false);

    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    m_variance = new QLabel;
    m_variance->setWordWrap(true);

    m_table = new QTableWidget;
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("الوقت"), QStringLiteral("النوع"), QStringLiteral("المبلغ"), QStringLiteral("ملاحظة")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(m_openButton);
    toolbar->addWidget(m_closeButton);
    toolbar->addStretch(1);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addLayout(toolbar);
    layout->addWidget(m_summary);
    layout->addWidget(m_table);
    layout->addWidget(m_variance);

    connect(m_openButton, &QPushButton::clicked, this, &CashSessionPage::onOpenClicked);
    connect(m_closeButton, &QPushButton::clicked, this, &CashSessionPage::onCloseClicked);

    refresh();
}

void CashSessionPage::refresh()
{
    m_table->setRowCount(0);
    m_variance->clear();
    m_sessionId = 0;
    m_expectedCents = 0;
    m_hasOpen = false;

    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_summary->setText(QStringLiteral("لا توجد جلسة مفتوحة حالياً."));
        m_openButton->setEnabled(true);
        m_closeButton->setEnabled(false);
        return;
    }

    m_hasOpen = true;
    m_sessionId = session->id;

    data::CashMovementRepository movements(m_db);
    const long long movementSum = movements.sumBySessionId(session->id);
    m_expectedCents = core::CashSessionCalculator::expectedTotalCents(session->openingFloatCents, movementSum);
    m_summary->setText(QStringLiteral("الجلسة #%1 — مفتوحة. رأس الفتح: %2  |  مجموع الحركات: %3  |  الموجود المتوقع: %4")
                           .arg(session->id)
                           .arg(formatMoney(session->openingFloatCents))
                           .arg(formatMoney(movementSum))
                           .arg(formatMoney(m_expectedCents)));

    for (const core::CashMovement& movement : movements.findBySessionId(session->id)) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(movement.createdAt.toString(QStringLiteral("HH:mm"))));
        m_table->setItem(row, 1, new QTableWidgetItem(movementTypeLabel(movement.type)));
        m_table->setItem(row, 2, new QTableWidgetItem(formatMoney(movement.amountCents)));
        m_table->setItem(row, 3, new QTableWidgetItem(movement.note));
    }

    m_openButton->setEnabled(false);
    m_closeButton->setEnabled(true);
}

bool CashSessionPage::hasOpenSession() const
{
    return m_hasOpen;
}

int CashSessionPage::sessionId() const
{
    return m_sessionId;
}

int CashSessionPage::movementCount() const
{
    return m_table->rowCount();
}

long long CashSessionPage::expectedCents() const
{
    return m_expectedCents;
}

long long CashSessionPage::lastVarianceCents() const
{
    return m_lastVarianceCents;
}

void CashSessionPage::openSession(long long openingFloatCents)
{
    data::CashSessionRepository sessions(m_db);
    if (sessions.findOpen()) {
        refresh();
        return;
    }
    const int id = sessions.open(openingFloatCents);
    if (id == 0) {
        m_summary->setText(QStringLiteral("تعذر فتح الجلسة."));
        return;
    }
    m_lastVarianceCents = 0;
    m_variance->clear();
    refresh();
}

void CashSessionPage::closeSession(long long closingCountedCents)
{
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        refresh();
        return;
    }
    data::CashMovementRepository movements(m_db);
    const long long movementSum = movements.sumBySessionId(session->id);
    const long long expected = core::CashSessionCalculator::expectedTotalCents(session->openingFloatCents, movementSum);
    const long long variance = core::CashSessionCalculator::varianceCents(closingCountedCents, expected);
    if (!sessions.close(session->id, closingCountedCents, expected, variance)) {
        m_summary->setText(QStringLiteral("تعذر إغلاق الجلسة."));
        return;
    }
    m_lastVarianceCents = variance;
    m_variance->setText(QStringLiteral("المعدود: %1  |  المتوقع: %2  |  الفرق: %3 (%4)")
                            .arg(formatMoney(closingCountedCents))
                            .arg(formatMoney(expected))
                            .arg(formatMoney(variance),
                                 variance < 0 ? QStringLiteral("عجز في الصندوق") : QStringLiteral("زيادة في الصندوق")));
    refresh();
}

void CashSessionPage::onOpenClicked()
{
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, QStringLiteral("بدء جلسة"), QStringLiteral("رصيد الفتح:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return;
    }
    const auto cents = parseMoney(text);
    if (!cents) {
        QMessageBox::warning(this, QStringLiteral("خطأ"), QStringLiteral("المبلغ غير صالح"));
        return;
    }
    openSession(*cents);
}

void CashSessionPage::onCloseClicked()
{
    const std::optional<core::CashSession> session = data::CashSessionRepository(m_db).findOpen();
    if (!session) {
        return;
    }
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, QStringLiteral("إغلاق الجلسة"), QStringLiteral("المبلغ المعدود في الصندوق:"), QLineEdit::Normal,
        QString(), &ok);
    if (!ok) {
        return;
    }
    const auto cents = parseMoney(text);
    if (!cents) {
        QMessageBox::warning(this, QStringLiteral("خطأ"), QStringLiteral("المبلغ غير صالح"));
        return;
    }
    closeSession(*cents);
}

} // namespace app::ui