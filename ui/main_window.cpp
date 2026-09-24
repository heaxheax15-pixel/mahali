#include "main_window.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QStackedWidget>
#include <QStatusBar>
#include <QWidget>

#include "cash_session_page.h"
#include "customers_page.h"
#include "format_utils.h"
#include "pos_page.h"
#include "products_page.h"
#include "sales_page.h"
#include "suppliers_page.h"

namespace app::ui {

MainWindow::MainWindow(app::data::Database& db, ServerController& controller, QWidget* parent)
    : QMainWindow(parent)
    , m_db(db)
    , m_controller(controller)
{
    setWindowTitle(QStringLiteral("محلي — نظام نقاط البيع والمحاسبة"));
    setLayoutDirection(Qt::RightToLeft);
    resize(1024, 640);

    m_nav = new QListWidget;
    m_nav->setFixedWidth(180);
    m_nav->addItem(QStringLiteral("البيع السريع"));
    m_nav->addItem(QStringLiteral("المنتجات"));
    m_nav->addItem(QStringLiteral("العملاء"));
    m_nav->addItem(QStringLiteral("الموردون"));
    m_nav->addItem(QStringLiteral("جلسة الصندوق"));
    m_nav->addItem(QStringLiteral("مبيعات اليوم"));
    m_nav->setCurrentRow(0);

    m_pages = new QStackedWidget;
    m_pos = new PosPage(db);
    m_cashSession = new CashSessionPage(db);
    m_sales = new SalesPage(db);
    m_pages->addWidget(m_pos);
    m_pages->addWidget(new ProductsPage(db));
    m_pages->addWidget(new CustomersPage(db));
    m_pages->addWidget(new SuppliersPage(db));
    m_pages->addWidget(m_cashSession);
    m_pages->addWidget(m_sales);

    auto* central = new QWidget;
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_nav);
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);

    connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_nav, &QListWidget::currentRowChanged, this,
            [this](int row) {
                // Live pages refresh each time the operator opens them.
                if (row == 4) {
                    m_cashSession->refresh();
                } else if (row == 5) {
                    m_sales->refresh();
                }
            });

    m_statusLabel = new QLabel;
    statusBar()->addWidget(m_statusLabel);
    connect(&m_controller, &ServerController::statsChanged, this, &MainWindow::onSyncStatusChanged);
    onSyncStatusChanged();
}

void MainWindow::onSyncStatusChanged()
{
    const ServerController::Stats stats = m_controller.stats();
    QString text;
    if (stats.listening) {
        text = QStringLiteral("خادم المزامنة: يعمل على المنفذ %1").arg(stats.port);
    } else {
        text = QStringLiteral("خادم المزامنة: متوقف");
    }
    text += QStringLiteral("  |  عمليات منفّذة حتى اليوم: %1").arg(stats.appliedOps);
    text += QStringLiteral("  |  أجهزة متصلة: %1").arg(stats.devices);
    text += QStringLiteral("  |  مبيعات اليوم: %1 (%2)")
                .arg(stats.salesToday)
                .arg(formatMoney(stats.revenueTodayCents));
    m_statusLabel->setText(text);
}

} // namespace app::ui