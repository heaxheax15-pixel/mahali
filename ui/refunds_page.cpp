#include "refunds_page.h"

#include <QDateTime>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTime>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

#include "core/audit_log_entry.h"
#include "data/audit_log_repository.h"
#include "data/cash_session_repository.h"
#include "data/customer_repository.h"
#include "data/payment_repository.h"
#include "data/payment_service.h"
#include "data/sale_repository.h"
#include "data/sale_service.h"
#include "format_utils.h"
#include "widgets/app_icon.h"
#include "widgets/page_header.h"
#include "widgets/ui_helpers.h"

namespace app::ui {

namespace {

void writeAudit(app::data::Database& db, const QString& action, const QString& target)
{
    core::AuditLogEntry entry;
    entry.actor = QStringLiteral("desktop");
    entry.action = action;
    entry.target = target;
    entry.createdAt = QDateTime::currentDateTime();
    app::data::AuditLogRepository(db).insert(entry);
}

} // namespace

RefundsPage::RefundsPage(app::data::Database& db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
{
    m_salesTable = new QTableWidget;
    m_salesTable->setAlternatingRowColors(true);
    m_salesTable->setColumnCount(4);
    m_salesTable->setHorizontalHeaderLabels(
        {QStringLiteral("الوقت"), QStringLiteral("المصدر"), QStringLiteral("الإجمالي"), QStringLiteral("الحالة")});
    m_salesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_salesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_salesTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_salesTable->horizontalHeader()->setStretchLastSection(true);

    m_refundSale = new QPushButton(QStringLiteral("استرداد المبيع"));
    m_refundSale->setObjectName(QStringLiteral("danger"));
    m_refundSale->setIcon(appIcon(Icon::Return, QColor(QStringLiteral("#ffffff")), 18));
    m_refundSale->setEnabled(false);
    connect(m_refundSale, &QPushButton::clicked, this, &RefundsPage::onRefundSaleClicked);
    connect(m_salesTable, &QTableWidget::itemSelectionChanged, this,
            [this]() { m_refundSale->setEnabled(m_salesTable->currentRow() >= 0); });

    auto* refundSaleRow = new QHBoxLayout;
    refundSaleRow->addWidget(new QLabel(QStringLiteral("مبيعات اليوم (تُعرض الأصول فقط):")));
    refundSaleRow->addStretch(1);
    refundSaleRow->addWidget(m_refundSale);

    auto* salesCard = makeCard();
    auto* salesLayout = new QVBoxLayout(salesCard);
    salesLayout->setContentsMargins(14, 12, 14, 14);
    salesLayout->setSpacing(8);
    salesLayout->addWidget(makeCardTitle(QStringLiteral("استرداد مبيع")));
    salesLayout->addLayout(refundSaleRow);
    salesLayout->addWidget(m_salesTable, 1);

    m_paymentsTable = new QTableWidget;
    m_paymentsTable->setAlternatingRowColors(true);
    m_paymentsTable->setColumnCount(3);
    m_paymentsTable->setHorizontalHeaderLabels({QStringLiteral("الوقت"), QStringLiteral("العميل"), QStringLiteral("المبلغ")});
    m_paymentsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_paymentsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_paymentsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_paymentsTable->horizontalHeader()->setStretchLastSection(true);

    m_refundPayment = new QPushButton(QStringLiteral("استرداد السداد"));
    m_refundPayment->setObjectName(QStringLiteral("danger"));
    m_refundPayment->setIcon(appIcon(Icon::Return, QColor(QStringLiteral("#ffffff")), 18));
    m_refundPayment->setEnabled(false);
    connect(m_refundPayment, &QPushButton::clicked, this, &RefundsPage::onRefundPaymentClicked);
    connect(m_paymentsTable, &QTableWidget::itemSelectionChanged, this,
            [this]() { m_refundPayment->setEnabled(m_paymentsTable->currentRow() >= 0); });

    auto* refundPaymentRow = new QHBoxLayout;
    refundPaymentRow->addWidget(new QLabel(QStringLiteral("سدايدات العملاء اليوم:")));
    refundPaymentRow->addStretch(1);
    refundPaymentRow->addWidget(m_refundPayment);

    auto* paymentsCard = makeCard();
    auto* paymentsLayout = new QVBoxLayout(paymentsCard);
    paymentsLayout->setContentsMargins(14, 12, 14, 14);
    paymentsLayout->setSpacing(8);
    paymentsLayout->addWidget(makeCardTitle(QStringLiteral("استرداد سداد عميل")));
    paymentsLayout->addLayout(refundPaymentRow);
    paymentsLayout->addWidget(m_paymentsTable, 1);

    m_notice = new QLabel;
    m_notice->setWordWrap(true);
    m_notice->setObjectName(QStringLiteral("noticeOk"));

    auto* root = new QVBoxLayout(this);
    padPageLayout(root);
    root->addWidget(new PageHeader(QStringLiteral("الاستردادات"),
                                   QStringLiteral("عكس مبيع أو إرجاع سداد عميل")));
    root->addWidget(salesCard, 1);
    root->addWidget(paymentsCard, 1);
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
    m_salesTable->setRowCount(0);
    for (const core::Sale& sale : todaySales) {
        if (sale.totalCents <= 0 || sale.reversedSaleId != 0 || reversedIds.count(sale.id) != 0) {
            continue;
        }
        const int row = m_salesTable->rowCount();
        m_salesTable->insertRow(row);
        m_salesTable->setItem(row, 0, new QTableWidgetItem(sale.createdAt.toString(QStringLiteral("HH:mm"))));
        m_salesTable->setItem(row, 1,
                              new QTableWidgetItem(sale.deviceId == QLatin1String("desktop")
                                                       ? QStringLiteral("الحاسوب")
                                                       : QStringLiteral("جهاز %1").arg(sale.deviceId.left(8))));
        m_salesTable->setItem(row, 2, new QTableWidgetItem(formatMoney(sale.totalCents)));
        m_salesTable->setItem(row, 3, new QTableWidgetItem(QStringLiteral("بيع")));
        m_salesTable->item(row, 0)->setData(Qt::UserRole, sale.id);
    }

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
        if (payment.amountCents <= 0 || payment.reversedId != 0 || reversedPaymentIds.count(payment.id) != 0) {
            continue;
        }
        const auto customer = customers.findById(payment.customerId);
        const int row = m_paymentsTable->rowCount();
        m_paymentsTable->insertRow(row);
        m_paymentsTable->setItem(row, 0, new QTableWidgetItem(payment.createdAt.toString(QStringLiteral("HH:mm"))));
        m_paymentsTable->setItem(row, 1,
                                 new QTableWidgetItem(customer ? customer->name
                                                               : QStringLiteral("زبون #%1").arg(payment.customerId)));
        m_paymentsTable->setItem(row, 2, new QTableWidgetItem(formatMoney(payment.amountCents)));
        m_paymentsTable->item(row, 0)->setData(Qt::UserRole, payment.id);
    }
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

void RefundsPage::onRefundSaleClicked()
{
    const int row = m_salesTable->currentRow();
    if (row < 0) {
        return;
    }
    const int saleId = m_salesTable->item(row, 0)->data(Qt::UserRole).toInt();
    if (QMessageBox::question(this, QStringLiteral("استرداد"),
                              QStringLiteral("هل تريد استرداد هذا المبيع وإرجاع البضاعة للرف؟")) ==
        QMessageBox::Yes) {
        refundSale(saleId);
    }
}

void RefundsPage::onRefundPaymentClicked()
{
    const int row = m_paymentsTable->currentRow();
    if (row < 0) {
        return;
    }
    const auto payment = m_paymentsTable->item(row, 0)->data(Qt::UserRole).toInt();
    if (QMessageBox::question(this, QStringLiteral("استرداد"),
                              QStringLiteral("هل تريد استرداد مبلغ هذا السداد للعميل؟")) == QMessageBox::Yes) {
        bool ok = false;
        const QString note =
            QInputDialog::getText(this, QStringLiteral("استرداد سداد"), QStringLiteral("ملاحظة (اختياري):"),
                                  QLineEdit::Normal, QString(), &ok);
        refundPayment(payment, ok ? note : QString());
    }
}

void RefundsPage::refundSale(int saleId)
{
    m_notice->clear();
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(QStringLiteral("لا توجد جلسة مفتوحة — افتح جلسة الصندوق أولاً"));
        return;
    }
    data::SaleService service(m_db);
    const int reversalId = service.reverseSale(saleId, session->id);
    if (reversalId == 0) {
        m_notice->setText(QStringLiteral("تعذر استرداد المبيع"));
        return;
    }
    data::SaleRepository sales(m_db);
    const auto original = sales.findById(saleId);
    writeAudit(m_db, QStringLiteral("sale_refund"),
               original ? QStringLiteral("مبيع #%1 (%2)").arg(saleId).arg(formatMoney(original->totalCents))
                        : QStringLiteral("مبيع #%1").arg(saleId));
    m_notice->setText(QStringLiteral("تم الاسترداد وعادت البضاعة للرف"));
    refresh();
}

void RefundsPage::refundPayment(int paymentId, const QString& note)
{
    m_notice->clear();
    data::CashSessionRepository sessions(m_db);
    const auto session = sessions.findOpen();
    if (!session) {
        m_notice->setText(QStringLiteral("لا توجد جلسة مفتوحة — افتح جلسة الصندوق أولاً"));
        return;
    }
    data::PaymentService service(m_db);
    const data::PaymentResult result = service.refundCustomerPayment(paymentId, session->id, note);
    if (!result.ok) {
        m_notice->setText(QStringLiteral("تعذر استرداد السداد: %1").arg(result.error));
        return;
    }
    writeAudit(m_db, QStringLiteral("customer_payment_refund"),
               QStringLiteral("سداد #%1").arg(paymentId));
    m_notice->setText(QStringLiteral("تم استرداد السداد"));
    refresh();
}

} // namespace app::ui