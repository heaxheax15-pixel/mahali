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
#include "core/cash_movement.h"
#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "format_utils.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/stat_card.h"
#include "widgets/ui_helpers.h"

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
    m_openButton->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    m_closeButton = new QPushButton(QStringLiteral("أغلق الجلسة"));
    m_closeButton->setObjectName(QStringLiteral("danger"));
    m_closeButton->setEnabled(false);

    m_header = new PageHeader(QStringLiteral("جلسة الصندوق"),
                              QStringLiteral("جلسة يومية مشتركة — تفتح بالرأس المبدئي وتُغلق بالجرد"));
    m_header->addAction(m_openButton);
    m_header->addAction(m_closeButton);

    m_floatCard = new StatCard(QStringLiteral("رأس الفتح"));
    m_floatCard->setIcon(Icon::Wallet, QStringLiteral("#0e7c75"));
    m_movementsCard = new StatCard(QStringLiteral("مجموع الحركات"));
    m_movementsCard->setIcon(Icon::Receipt, QStringLiteral("#c8860f"));
    m_expectedCard = new StatCard(QStringLiteral("الموجود المتوقع"));
    m_expectedCard->setIcon(Icon::Clock, QStringLiteral("#1d5f9e"));
    m_varianceCard = new StatCard(QStringLiteral("الفرق (عند الجرد)"));
    m_varianceCard->setIcon(Icon::BarChart, QStringLiteral("#c84444"));

    auto* cards = new QHBoxLayout;
    cards->setSpacing(10);
    for (StatCard* card : {m_floatCard, m_movementsCard, m_expectedCard, m_varianceCard}) {
        cards->addWidget(card, 1);
    }

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_variance = new QLabel(this);
    m_variance->setWordWrap(true);
    m_summary->hide();
    m_variance->hide();

    m_table = new QTableWidget;
    m_table->setAlternatingRowColors(true);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("الوقت"), QStringLiteral("النوع"), QStringLiteral("المبلغ"), QStringLiteral("ملاحظة")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    auto* movementsCard = makeCard();
    auto* movementsLayout = new QVBoxLayout(movementsCard);
    movementsLayout->setContentsMargins(14, 12, 14, 14);
    movementsLayout->setSpacing(8);
    movementsLayout->addWidget(makeCardTitle(QStringLiteral("حركات الجلسة")));
    movementsLayout->addWidget(m_table, 1);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(m_header);
    root->addLayout(cards);
    root->addWidget(movementsCard, 1);

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
        m_header->setSubtitle(QStringLiteral("لا توجد جلسة مفتوحة حالياً — ابدأ جلسة برصيد الفتح."));
        for (StatCard* card : {m_floatCard, m_movementsCard, m_expectedCard, m_varianceCard}) {
            card->setValue(QStringLiteral("—"));
        }
        if (m_lastVarianceCents != 0) {
            m_varianceCard->setDelta(m_lastVarianceCents);
            m_header->setSubtitle(QStringLiteral("آخر جلسة أُغلقت بفرق %1")
                                      .arg(formatMoney(m_lastVarianceCents)));
        }
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
    m_header->setSubtitle(QStringLiteral("الجلسة #%1 مفتوحة منذ %2")
                              .arg(session->id)
                              .arg(session->openedAt.toString(QStringLiteral("HH:mm"))));
    m_floatCard->setCents(session->openingFloatCents);
    m_movementsCard->setCents(movementSum);
    m_expectedCard->setCents(m_expectedCents);
    m_varianceCard->setValue(m_hasOpen ? QStringLiteral("جارية") : QStringLiteral("—"));
    m_summary->setText(QStringLiteral("الجلسة #%1 — مفتوحة: %2  |  %3  |  %4")
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