#include "refunds_page.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTime>
#include <QVBoxLayout>

#include <algorithm>
#include <set>
#include <vector>

#include "core/audit_log_entry.h"
#include "core/session.h"
#include "data/audit_log_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/payment_repository.h"
#include "data/payment_service.h"
#include "data/sale_item_repository.h"
#include "data/sale_repository.h"
#include "data/sale_service.h"
#include "data/setting_repository.h"
#include "format_utils.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

void writeAudit(app::data::Database& db, const QString& action, const QString& target)
{
    core::AuditLogEntry entry;
    entry.actor = app::core::Session::instance().actorName();
    entry.action = action;
    entry.target = target;
    entry.createdAt = QDateTime::currentDateTime();
    app::data::AuditLogRepository(db).insert(entry);
}

// The one button on a row: a symbol rather than the word, because the column is
// 60px wide and "Rembourser" does not fit it at a readable size.
//
// The colour arrives as a parameter instead of being read here: a free function
// has no database to read the setting from, and passing one in just to pick a
// colour would couple a widget factory to the data layer for the sake of one
// QColor. The caller asks once and reuses the answer for every row.
QPushButton* makeRowRefundButton(QWidget* parent, const QColor& iconColor)
{
    auto* button = new QPushButton(parent);
    button->setObjectName(QStringLiteral("rowRefundButton"));
    button->setIcon(appIcon(Icon::Return, iconColor, 16));
    button->setIconSize(QSize(16, 16));
    button->setFixedSize(32, 32);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(QCoreApplication::translate("RefundsPage", "Rembourser"));
    return button;
}

} // namespace

QColor RefundsPage::rowIconColor() const
{
    // The setting the settings page's own selector writes, so the two cannot
    // disagree about which theme the shop is running.
    data::SettingRepository settings(m_db);
    const QString theme =
        settings.value(QStringLiteral("theme")).value_or(QStringLiteral("light"));
    return theme == QStringLiteral("dark") ? QColor(QStringLiteral("#a3a3a3"))
                                           : QColor(QStringLiteral("#475569"));
}

void RefundsPage::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::ThemeChange
        || event->type() == QEvent::PaletteChange) {
        repaintRowButtons();
    }
}

void RefundsPage::repaintRowButtons()
{
    // Re-tinted rather than rebuilt: the rows carry the ids in their button
    // closures, and rebuilding here would replace the closure a confirmation
    // dialog is holding open.
    const QColor color = rowIconColor();
    for (QPushButton* button : m_rowButtons) {
        if (button) {
            button->setIcon(appIcon(Icon::Return, color, 16));
        }
    }
}

RefundsPage::RefundsPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_salesTable = new QTableWidget;
    m_salesTable->setObjectName(QStringLiteral("refundSalesTable"));
    m_salesTable->setAlternatingRowColors(true);
    m_salesTable->setFrameShape(QFrame::NoFrame);
    m_salesTable->setShowGrid(true);
    m_salesTable->setColumnCount(4);
    m_salesTable->setHorizontalHeaderLabels(
        {tr("الساعة"), tr("الإجمالي"), tr("Articles"), tr("")});
    m_salesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_salesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_salesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_salesTable->verticalHeader()->setDefaultSectionSize(42);
    m_salesTable->verticalHeader()->hide();
    // Action last, so it reads as the tail of each row rather than as a second
    // quantity beside Total: stretching anything else would squeeze the money.
    m_salesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_salesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_salesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_salesTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_salesTable->setColumnWidth(0, 90);
    m_salesTable->setColumnWidth(1, 120);
    m_salesTable->setColumnWidth(2, 100);
    m_salesTable->setColumnWidth(3, 60);

    m_salesEmpty = new QLabel(tr("لا يوجد بيع للاسترداد اليوم"));
    m_salesEmpty->setObjectName(QStringLiteral("faintText"));
    m_salesEmpty->setAlignment(Qt::AlignCenter);

    auto* salesTab = new QWidget;
    auto* salesTabLayout = new QVBoxLayout(salesTab);
    salesTabLayout->setContentsMargins(0, 14, 0, 0);
    salesTabLayout->setSpacing(0);
    salesTabLayout->addWidget(m_salesEmpty, 1);
    salesTabLayout->addWidget(m_salesTable, 1);

    m_paymentsTable = new QTableWidget;
    m_paymentsTable->setObjectName(QStringLiteral("refundPaymentsTable"));
    m_paymentsTable->setAlternatingRowColors(true);
    m_paymentsTable->setFrameShape(QFrame::NoFrame);
    m_paymentsTable->setShowGrid(true);
    m_paymentsTable->setColumnCount(4);
    m_paymentsTable->setHorizontalHeaderLabels(
        {tr("الساعة"), tr("المبلغ"), tr("الزبون"), tr("")});
    m_paymentsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_paymentsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_paymentsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_paymentsTable->verticalHeader()->setDefaultSectionSize(42);
    m_paymentsTable->verticalHeader()->hide();
    m_paymentsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_paymentsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_paymentsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_paymentsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    m_paymentsTable->setColumnWidth(0, 90);
    m_paymentsTable->setColumnWidth(1, 120);
    m_paymentsTable->setColumnWidth(2, 260);
    m_paymentsTable->setColumnWidth(3, 60);

    m_paymentsEmpty = new QLabel(tr("لا يوجد سداد للاسترداد اليوم"));
    m_paymentsEmpty->setObjectName(QStringLiteral("faintText"));
    m_paymentsEmpty->setAlignment(Qt::AlignCenter);

    auto* paymentsTab = new QWidget;
    auto* paymentsTabLayout = new QVBoxLayout(paymentsTab);
    paymentsTabLayout->setContentsMargins(0, 14, 0, 0);
    paymentsTabLayout->setSpacing(0);
    paymentsTabLayout->addWidget(m_paymentsEmpty, 1);
    paymentsTabLayout->addWidget(m_paymentsTable, 1);

    auto* tabs = new QTabWidget;
    tabs->addTab(salesTab, tr("Ventes"));
    tabs->addTab(paymentsTab, tr("Règlements clients"));

    m_notice = new QLabel;
    m_notice->setWordWrap(true);
    m_notice->setMinimumHeight(42);
    m_notice->setObjectName(QStringLiteral("noticeOk"));
    m_notice->setVisible(false);

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(tr("الاستردادات"),
                                   tr("عكس مبيع أو إرجاع سداد عميل")));
    root->addWidget(tabs, 1);
    root->addWidget(m_notice);

    refresh();
}

void RefundsPage::refresh()
{
    data::SaleRepository sales(m_db);
    data::PaymentRepository payments(m_db);
    data::CustomerRepository customers(m_db);

    const QDateTime now = QDateTime::currentDateTime();
    QDateTime dayStart = now;
    dayStart.setTime(QTime(0, 0, 0));

    auto todaySales = sales.findBetween(dayStart, now);
    std::sort(todaySales.begin(), todaySales.end(),
              [](const core::Sale& a, const core::Sale& b) { return a.createdAt > b.createdAt; });
    std::set<int> reversedIds;
    for (const core::Sale& sale : todaySales) {
        if (sale.reversedSaleId != 0) {
            reversedIds.insert(sale.reversedSaleId);
        }
    }

    // Which sales are actually refundable, worked out before anything is built:
    // the item counts are one query over exactly these ids, and querying the
    // whole day's sales would fetch lines for rows already filtered out.
    std::vector<core::Sale> refundable;
    for (const core::Sale& sale : todaySales) {
        if (sale.totalCents <= 0 || sale.reversedSaleId != 0 || reversedIds.count(sale.id) != 0) {
            continue;
        }
        refundable.push_back(sale);
    }

    std::vector<int> saleIds;
    saleIds.reserve(refundable.size());
    for (const core::Sale& sale : refundable) {
        saleIds.push_back(sale.id);
    }
    const auto itemQuantities = data::SaleItemRepository(m_db).findQuantitiesBySaleIds(saleIds);
    const QColor iconColor = rowIconColor();
    // Rebuilt from scratch every refresh: the old buttons are about to be deleted
    // with their rows, and holding them here would keep the previous day's rows
    // reachable for a repaint that has nothing to paint.
    m_rowButtons.clear();

    m_salesTable->setRowCount(0);
    for (const core::Sale& sale : refundable) {
        const int row = m_salesTable->rowCount();
        m_salesTable->insertRow(row);
        m_salesTable->setItem(
            row, 0, new QTableWidgetItem(sale.createdAt.toString(QStringLiteral("HH:mm"))));
        m_salesTable->setItem(row, 1, new QTableWidgetItem(formatMoney(sale.totalCents)));
        const auto quantity = itemQuantities.find(sale.id);
        m_salesTable->setItem(
            row, 2, new QTableWidgetItem(quantity == itemQuantities.end()
                                             ? QStringLiteral("—")
                                             : QString::number(quantity->second)));
        // The id rides on the row button's closure rather than on a cell's
        // UserRole: the button outlives any sorting the user does, and reading
        // the id back off item(row, 0) would mean a re-sort could silently
        // reverse a different sale than the one that was pressed.
        const int saleId = sale.id;
        auto* button = makeRowRefundButton(m_salesTable, iconColor);
        button->setToolTip(
            QCoreApplication::translate("RefundsPage", "Rembourser cette vente"));
        connect(button, &QPushButton::clicked, this, [this, saleId]() { confirmSaleRefund(saleId); });
        m_salesTable->setCellWidget(row, 3, button);
        m_rowButtons.append(button);
    }
    m_salesTable->setVisible(m_salesTable->rowCount() > 0);
    m_salesEmpty->setVisible(m_salesTable->rowCount() == 0);

    auto todayPayments = payments.findBetween(dayStart, now);
    std::sort(todayPayments.begin(), todayPayments.end(),
              [](const core::Payment& a, const core::Payment& b) { return a.createdAt > b.createdAt; });
    std::set<int> reversedPaymentIds;
    for (const core::Payment& payment : todayPayments) {
        if (payment.reversedId != 0) {
            reversedPaymentIds.insert(payment.reversedId);
        }
    }
    m_paymentsTable->setRowCount(0);
    for (const core::Payment& payment : todayPayments) {
        if (payment.amountCents <= 0 || payment.reversedId != 0
            || reversedPaymentIds.count(payment.id) != 0) {
            continue;
        }
        const auto customer = customers.findById(payment.customerId);
        const int row = m_paymentsTable->rowCount();
        m_paymentsTable->insertRow(row);
        m_paymentsTable->setItem(
            row, 0, new QTableWidgetItem(payment.createdAt.toString(QStringLiteral("HH:mm"))));
        m_paymentsTable->setItem(row, 1, new QTableWidgetItem(formatMoney(payment.amountCents)));
        m_paymentsTable->setItem(
            row, 2,
            new QTableWidgetItem(customer ? customer->name
                                         : tr("زبون #%1").arg(payment.customerId)));
        const int paymentId = payment.id;
        auto* button = makeRowRefundButton(m_paymentsTable, iconColor);
        button->setToolTip(
            QCoreApplication::translate("RefundsPage", "Rembourser ce règlement"));
        connect(button, &QPushButton::clicked, this,
                [this, paymentId]() { confirmPaymentRefund(paymentId); });
        m_paymentsTable->setCellWidget(row, 3, button);
        m_rowButtons.append(button);
    }
    m_paymentsTable->setVisible(m_paymentsTable->rowCount() > 0);
    m_paymentsEmpty->setVisible(m_paymentsTable->rowCount() == 0);
}

int RefundsPage::salesRowCount() const
{
    return m_salesTable->rowCount();
}

int RefundsPage::paymentsRowCount() const
{
    return m_paymentsTable->rowCount();
}

QString RefundsPage::noticeText() const
{
    return m_notice->text();
}

void RefundsPage::confirmSaleRefund(int saleId)
{
    if (QMessageBox::question(this, tr("استرداد"),
                              tr("هل تريد استرداد هذا المبيع وإرجاع البضاعة للرف؟")) ==
        QMessageBox::Yes) {
        refundSale(saleId);
    }
}

void RefundsPage::confirmPaymentRefund(int paymentId)
{
    if (QMessageBox::question(this, tr("استرداد"),
                              tr("هل تريد استرداد مبلغ هذا السداد للعميل؟")) == QMessageBox::Yes) {
        bool ok = false;
        const QString note =
            QInputDialog::getText(this, tr("استرداد سداد"), tr("ملاحظة (اختياري):"),
                                  QLineEdit::Normal, QString(), &ok);
        refundPayment(paymentId, ok ? note : QString());
    }
}

void RefundsPage::refundSale(int saleId)
{
    m_notice->clear();
    m_notice->setVisible(false);
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(tr("لا توجد جلسة مفتوحة — افتح جلسة الصندوق أولاً"));
        m_notice->setVisible(!m_notice->text().isEmpty());
        return;
    }
    data::SaleService service(m_db);
    const data::SaleReverseResult result = service.reverseSale(saleId, session->id);
    if (!result.ok) {
        m_notice->setText(tr("تعذر استرداد المبيع: %1").arg(result.error));
        m_notice->setVisible(!m_notice->text().isEmpty());
        return;
    }
    data::SaleRepository sales(m_db);
    const auto original = sales.findById(saleId);
    writeAudit(m_db, QStringLiteral("sale_refund"),
original ? tr("مبيع #%1 (%2)").arg(saleId).arg(formatMoney(original->totalCents))
                         : tr("مبيع #%1").arg(saleId));
    m_notice->setText(tr("تم الاسترداد وعادت البضاعة للرف"));
    m_notice->setVisible(!m_notice->text().isEmpty());
    refresh();
}

void RefundsPage::refundPayment(int paymentId, const QString& note)
{
    m_notice->clear();
    m_notice->setVisible(false);
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(tr("لا توجد جلسة مفتوحة — افتح جلسة الصندوق أولاً"));
        m_notice->setVisible(!m_notice->text().isEmpty());
        return;
    }
    data::PaymentService service(m_db);
    const data::PaymentResult result = service.refundCustomerPayment(paymentId, session->id, note);
    if (!result.ok) {
        m_notice->setText(tr("تعذر استرداد السداد: %1").arg(result.error));
        m_notice->setVisible(!m_notice->text().isEmpty());
        return;
    }
    writeAudit(m_db, QStringLiteral("customer_payment_refund"),
               tr("سداد #%1").arg(paymentId));
    m_notice->setText(tr("تم استرداد السداد"));
    m_notice->setVisible(!m_notice->text().isEmpty());
    refresh();
}

} // namespace app::ui