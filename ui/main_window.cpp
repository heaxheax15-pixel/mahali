#include "main_window.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "audit_log_page.h"
#include "cash_session_page.h"
#include "customers_page.h"
#include "expenses_page.h"
#include "format_utils.h"
#include "pos_page.h"
#include "products_page.h"
#include "refunds_page.h"
#include "reports_page.h"
#include "sales_page.h"
#include "settings_page.h"
#include "suppliers_page.h"
#include "theme.h"
#include "widgets/app_icon.h"

namespace app::ui {

namespace {

struct NavEntry {
    Icon icon;
    QString label;
    int pageIndex;
};

const std::vector<NavEntry>& navEntries()
{
    static const std::vector<NavEntry> entries = {
        {Icon::Cart, QStringLiteral("البيع السريع"), 0},
        {Icon::Box, QStringLiteral("المنتجات"), 1},
        {Icon::People, QStringLiteral("العملاء"), 2},
        {Icon::Truck, QStringLiteral("الموردون"), 3},
        {Icon::Wallet, QStringLiteral("جلسة الصندوق"), 4},
        {Icon::Receipt, QStringLiteral("مبيعات اليوم"), 5},
        {Icon::Tag, QStringLiteral("المصاريف والسحوبات"), 6},
        {Icon::BarChart, QStringLiteral("التقارير"), 7},
        {Icon::Return, QStringLiteral("الاستردادات"), 8},
        {Icon::History, QStringLiteral("سجل المراجعة"), 9},
        {Icon::Gear, QStringLiteral("الإعدادات"), 10},
    };
    return entries;
}

} // namespace

MainWindow::MainWindow(app::data::Database& db, ServerController& controller, QWidget* parent)
    : QMainWindow(parent)
    , m_db(db)
    , m_controller(controller)
{
    setWindowTitle(QStringLiteral("محلي — نظام نقاط البيع والمحاسبة"));
    setLayoutDirection(Qt::RightToLeft);
    resize(1360, 840);
    setMinimumSize(1120, 700);

    QIcon windowIcon;
    windowIcon.addFile(QStringLiteral(":/mahali/icons/app-512.png"), QSize(512, 512));
    windowIcon.addFile(QStringLiteral(":/mahali/icons/app-256.png"), QSize(256, 256));
    windowIcon.addFile(QStringLiteral(":/mahali/icons/app-128.png"), QSize(128, 128));
    windowIcon.addFile(QStringLiteral(":/mahali/icons/app-64.png"), QSize(64, 64));
    windowIcon.addFile(QStringLiteral(":/mahali/icons/app-48.png"), QSize(48, 48));
    windowIcon.addFile(QStringLiteral(":/mahali/icons/app-32.png"), QSize(32, 32));
    setWindowIcon(windowIcon);

    m_nav = new QListWidget;
    m_nav->setObjectName(QStringLiteral("nav"));
    m_nav->setIconSize(QSize(20, 20));
    for (const NavEntry& entry : navEntries()) {
        auto* item = new QListWidgetItem(appIcon(entry.icon, QColor(QStringLiteral("#2ba89e")), 20),
                                         entry.label);
        item->setSizeHint(QSize(0, 42));
        m_nav->addItem(item);
    }
    m_nav->setCurrentRow(0);

    auto* brandIcon = new QLabel;
    brandIcon->setPixmap(appIcon(Icon::Shop, QColor(QStringLiteral("#ffffff")), 24).pixmap(24, 24));
    auto* brandTitle = new QLabel(QStringLiteral("محلي"));
    brandTitle->setObjectName(QStringLiteral("appTitle"));
    auto* brandSub = new QLabel(QStringLiteral("نظام البيع والمحاسبة"));
    brandSub->setObjectName(QStringLiteral("appSub"));

    auto* brandTexts = new QVBoxLayout;
    brandTexts->setContentsMargins(0, 0, 0, 0);
    brandTexts->setSpacing(0);
    brandTexts->addWidget(brandTitle);
    brandTexts->addWidget(brandSub);

    auto* brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(16, 14, 16, 6);
    brandRow->setSpacing(10);
    brandRow->addWidget(brandIcon);
    brandRow->addLayout(brandTexts, 1);

    auto* about = new QPushButton(QStringLiteral("حول محلي…"));
    about->setObjectName(QStringLiteral("about"));
    about->setCursor(Qt::PointingHandCursor);
    connect(about, &QPushButton::clicked, this, [this]() {
        QMessageBox::about(
            this, QStringLiteral("حول محلي"),
            QStringLiteral("<h3>محلي — نظام نقاط البيع والمحاسبة</h3>"
                           "<p>إدارة البيع السريع، الجرد، حسابات العملاء والموردين، "
                           "جلسات الصندوق، المصاريف، التقارير والاستردادات — "
                           "بدون اتصال وبثيَمَين فاتح/داكن.</p>"
                           "<p><b>الإصدار:</b> %1</p>")
                .arg(qApp->applicationVersion()));
    });

    auto* sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(210);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 10);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addLayout(brandRow);
    sidebarLayout->addWidget(m_nav, 1);
    sidebarLayout->addWidget(about);

    m_pages = new QStackedWidget;
    m_pages->setObjectName(QStringLiteral("content"));
    m_pos = new PosPage(db);
    m_cashSession = new CashSessionPage(db);
    m_sales = new SalesPage(db);
    m_expenses = new ExpensesPage(db);
    m_reports = new ReportsPage(db);
    m_refunds = new RefundsPage(db);
    m_auditLog = new AuditLogPage(db);
    m_settings = new SettingsPage(db);
    m_pages->addWidget(m_pos);
    m_pages->addWidget(new ProductsPage(db));
    m_pages->addWidget(new CustomersPage(db));
    m_pages->addWidget(new SuppliersPage(db));
    m_pages->addWidget(m_cashSession);
    m_pages->addWidget(m_sales);
    m_pages->addWidget(m_expenses);
    m_pages->addWidget(m_reports);
    m_pages->addWidget(m_refunds);
    m_pages->addWidget(m_auditLog);
    m_pages->addWidget(m_settings);

    auto* central = new QWidget;
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(sidebar);
    layout->addWidget(m_pages, 1);
    setCentralWidget(central);

    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::onPageChanged);
    m_nav->setCurrentRow(0);

    m_statusLabel = new QLabel;
    statusBar()->addWidget(m_statusLabel);
    connect(&m_controller, &ServerController::statsChanged, this, &MainWindow::onSyncStatusChanged);
    onSyncStatusChanged();
}

void MainWindow::onPageChanged(int row)
{
    m_pages->setCurrentIndex(row);
    switch (row) {
    case 4:
        m_cashSession->refresh();
        break;
    case 5:
        m_sales->refresh();
        break;
    case 6:
        m_expenses->refresh();
        break;
    case 7:
        m_reports->refresh();
        break;
    case 8:
        m_refunds->refresh();
        break;
    case 9:
        m_auditLog->refresh();
        break;
    case 10:
        m_settings->refresh();
        break;
    default:
        break;
    }

    if (m_fade) {
        m_fade->stop();
    } else {
        m_fade = new QPropertyAnimation(m_pages, "windowOpacity", this);
        m_fade->setDuration(140);
        m_fade->setStartValue(0.35);
        m_fade->setEndValue(1.0);
        m_fade->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_fade->start();
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