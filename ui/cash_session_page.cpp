#include "cash_session_page.h"

#include <QCoreApplication>
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

#include "core/cash_movement.h"
#include "core/cash_session_calculator.h"
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
    if (type == core::cashMovementType::kSale) {
        return QCoreApplication::translate("app::ui::CashSessionPage", "بيع");
    }
    if (type == core::cashMovementType::kCustomerPayment) {
        return QCoreApplication::translate("app::ui::CashSessionPage", "دفعة عميل");
    }
    if (type == core::cashMovementType::kExpense) {
        return QCoreApplication::translate("app::ui::CashSessionPage", "مصروف");
    }
    if (type == core::cashMovementType::kDrawing) {
        return QCoreApplication::translate("app::ui::CashSessionPage", "سحب");
    }
    if (type == core::cashMovementType::kSupplierPayment) {
        return QCoreApplication::translate("app::ui::CashSessionPage", "سداد مورد");
    }
    if (type == core::cashMovementType::kRefund) {
        return QCoreApplication::translate("app::ui::CashSessionPage", "استرداد");
    }
    return type;
}

} // namespace

CashSessionPage::CashSessionPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_openButton = new QPushButton(tr("ابدأ جلسة"));
    m_openButton->setObjectName(QStringLiteral("primary"));
    m_openButton->setIcon(appIcon(Icon::Plus, QColor(QStringLiteral("#ffffff")), 18));
    m_closeButton = new QPushButton(tr("أغلق الجلسة"));
    m_closeButton->setObjectName(QStringLiteral("danger"));
    m_closeButton->setEnabled(false);

    m_header = new PageHeader(tr("جلسة الصندوق"),
                              tr("جلسة يومية مشتركة — تفتح بالرأس المبدئي وتُغلق بالجرد"));
    m_header->addAction(m_openButton);
    m_header->addAction(m_closeButton);

    m_floatCard = new StatCard(tr("رأس الفتح"));
    m_floatCard->setIcon(Icon::Wallet, QStringLiteral("#0e7c75"));
    m_movementsCard = new StatCard(tr("مجموع الحركات"));
    m_movementsCard->setIcon(Icon::Receipt, QStringLiteral("#c8860f"));
    m_expectedCard = new StatCard(tr("الموجود المتوقع"));
    m_expectedCard->setIcon(Icon::Clock, QStringLiteral("#1d5f9e"));
    m_varianceCard = new StatCard(tr("الفرق (عند الجرد)"));
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
    // Both bars are filled by refresh(); an empty one would still hold its place
    // in the layout, so neither starts visible.
    m_summary->setVisible(false);
    m_variance->setVisible(false);

    m_table = new QTableWidget;
    m_table->setObjectName(QStringLiteral("cashSessionTable"));
    m_table->setAlternatingRowColors(true);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setShowGrid(true);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("الوقت"), tr("النوع"), tr("المبلغ"), tr("ملاحظة")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->verticalHeader()->setDefaultSectionSize(42);
    m_table->verticalHeader()->hide();

    auto* movementsCard = makeCard();
    auto* movementsLayout = new QVBoxLayout(movementsCard);
    padCardLayout(movementsLayout);
    movementsLayout->addWidget(makeCardTitle(tr("حركات الجلسة")));
    movementsLayout->addWidget(m_table, 1);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(m_header);
    // The two lines used to be plain children of the page with no layout owning
    // them, so they kept the 100px placeholder geometry and the summary lost two
    // thirds of its text to the edge. Given a row of their own they take the full
    // content width; the one that wraps to two lines is the message, not the
    // heading, so word wrap stays on.
    root->addWidget(m_summary);
    root->addWidget(m_variance);
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
    m_variance->setVisible(false);
    m_sessionId = 0;
    m_expectedCents = 0;
    m_hasOpen = false;

    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_header->setSubtitle(tr("لا توجد جلسة مفتوحة حالياً — ابدأ جلسة برصيد الفتح."));
        for (StatCard* card : {m_floatCard, m_movementsCard, m_expectedCard, m_varianceCard}) {
            card->setValue(tr("—"));
        }
        if (m_lastVarianceCents != 0) {
            m_varianceCard->setDelta(m_lastVarianceCents);
            m_header->setSubtitle(tr("آخر جلسة أُغلقت بفرق %1")
                                      .arg(formatMoney(m_lastVarianceCents)));
        }
        m_summary->setText(tr("لا توجد جلسة مفتوحة حالياً."));
        m_summary->setVisible(!m_summary->text().isEmpty());
        m_openButton->setEnabled(true);
        m_closeButton->setEnabled(false);
        return;
    }

    m_hasOpen = true;
    m_sessionId = session->id;

    data::CashMovementRepository movements(m_db);
    const long long movementSum = movements.sumBySessionId(session->id);
    const int unreferencedCount = data::CashSessionRepository(m_db).unreferencedMovementCount(session->id);
    m_expectedCents = core::CashSessionCalculator::expectedTotalCents(session->openingFloatCents, movementSum);
    m_header->setSubtitle(tr("الجلسة #%1 مفتوحة منذ %2")
                              .arg(session->id)
                              .arg(session->openedAt.toString(QStringLiteral("HH:mm"))));
    m_floatCard->setCents(session->openingFloatCents);
    m_movementsCard->setCents(movementSum);
    m_expectedCard->setCents(m_expectedCents);
    m_varianceCard->setValue(m_hasOpen ? tr("جارية") : tr("—"));
    m_summary->setText(tr("الجلسة #%1 — مفتوحة: %2  |  %3  |  %4  |  حركات قديمة بلا مرجع: %5")
                           .arg(session->id)
                           .arg(formatMoney(session->openingFloatCents))
                           .arg(formatMoney(movementSum))
                           .arg(formatMoney(m_expectedCents))
                           .arg(unreferencedCount));
    m_summary->setVisible(!m_summary->text().isEmpty());

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
        m_summary->setText(tr("تعذر فتح الجلسة."));
        m_summary->setVisible(!m_summary->text().isEmpty());
        return;
    }
    m_lastVarianceCents = 0;
    m_variance->clear();
    m_variance->setVisible(false);
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
    const QVector<data::CashSessionViolation> violations = sessions.validateForClose(session->id);
    if (!sessions.close(session->id, closingCountedCents, expected, variance)) {
        m_summary->setText(tr("تعذر إغلاق الجلسة."));
        m_summary->setVisible(!m_summary->text().isEmpty());
        return;
    }
    m_lastVarianceCents = variance;
    m_variance->setText(tr("المعدود: %1  |  المتوقع: %2  |  الفرق: %3 (%4)")
                            .arg(formatMoney(closingCountedCents))
                            .arg(formatMoney(expected))
                            .arg(formatMoney(variance),
                                 variance < 0 ? tr("عجز في الصندوق") : tr("زيادة في الصندوق")));
    m_variance->setVisible(!m_variance->text().isEmpty());
    refresh();
    if (!violations.isEmpty()) {
        QStringList descriptions;
        descriptions.reserve(violations.size());
        for (const data::CashSessionViolation& violation : violations) {
            descriptions.push_back(QStringLiteral("[%1 #%2] %3")
                                       .arg(violation.entityType)
                                       .arg(violation.entityId)
                                       .arg(violation.description));
        }
        m_summary->setText(tr("أُغلقت الجلسة مع %1 مخالفة تدقيق:\n%2")
                               .arg(violations.size())
                               .arg(descriptions.join(QLatin1Char('\n'))));
        m_summary->setVisible(true);
    }
}

bool CashSessionPage::amountFromInput(const QString& text, long long* cents) const
{
    const auto parsed = parseMoney(text);
    // parseMoney already rejects junk and a leading '-', but zero is a valid
    // parse: it used to sail through and close the session with nothing
    // counted, which booked the whole till as a deficit and shut the operator
    // out of it for the day with no message anywhere.
    if (!parsed || *parsed <= 0) {
        return false;
    }
    *cents = *parsed;
    return true;
}

void CashSessionPage::onOpenClicked()
{
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, tr("بدء جلسة"), tr("رصيد الفتح:"), QLineEdit::Normal, QString(), &ok);
    if (!ok) {
        return;
    }
    long long cents = 0;
    if (!amountFromInput(text, &cents)) {
        // The dialog is gone by now, so there is no field to focus: the operator
        // clicks the button again and retypes. This page reports bad amounts
        // this way already.
        QMessageBox::warning(this, tr("خطأ"), tr("أدخل مبلغًا صحيحًا أكبر من صفر"));
        return;
    }
    openSession(cents);
}

void CashSessionPage::onCloseClicked()
{
    const std::optional<core::CashSession> session = data::CashSessionRepository(m_db).findOpen();
    if (!session) {
        return;
    }
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, tr("إغلاق الجلسة"), tr("المبلغ المعدود في الصندوق:"), QLineEdit::Normal,
        QString(), &ok);
    if (!ok) {
        return;
    }
    long long cents = 0;
    if (!amountFromInput(text, &cents)) {
        QMessageBox::warning(this, tr("خطأ"), tr("أدخل مبلغًا صحيحًا أكبر من صفر"));
        return;
    }
    closeSession(cents);
}

} // namespace app::ui