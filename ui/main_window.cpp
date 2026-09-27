#include "main_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <memory>

#include "core/session.h"
#include "core/update_checker.h"
#include "core/update_downloader.h"
#include "core/update_installer.h"
#include "audit_log_page.h"
#include "cash_session_page.h"
#include "customers_page.h"
#include "expenses_page.h"
#include "format_utils.h"
#include "login_dialog.h"
#include "pos_page.h"
#include "products_page.h"
#include "refunds_page.h"
#include "reports_page.h"
#include "sales_page.h"
#include "settings_page.h"
#include "suppliers_page.h"
#include "users_page.h"
#include "theme.h"
#include "widgets/app_icon.h"

namespace app::ui {

namespace {

struct NavEntry {
    Icon icon;
    QString label;
    int pageIndex;
};

} // namespace

MainWindow::MainWindow(app::data::Database& db, ServerController& controller, QWidget* parent)
    : QMainWindow(parent)
    , m_db(db)
    , m_controller(controller)
{
    setWindowTitle(tr("محلي — نظام نقاط البيع والمحاسبة"));
    setLayoutDirection(Qt::RightToLeft);

    setMinimumSize(900, 600);

    setWindowIcon(QIcon(QStringLiteral(":/mahali/icons/app.ico")));

    m_nav = new QListWidget;
    m_nav->setObjectName(QStringLiteral("nav"));
    m_nav->setIconSize(QSize(20, 20));
    m_nav->setSpacing(4);
    m_nav->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setTextElideMode(Qt::ElideRight);
    m_nav->setUniformItemSizes(false);

    auto* brandIcon = new QLabel;
    brandIcon->setObjectName(QStringLiteral("brandIcon"));
    brandIcon->setFixedSize(42, 42);
    brandIcon->setAlignment(Qt::AlignCenter);
    brandIcon->setPixmap(appIcon(Icon::Shop, QColor(QStringLiteral("#ffffff")), 24).pixmap(24, 24));
    auto* brandTitle = new QLabel(tr("محلي"));
    brandTitle->setObjectName(QStringLiteral("appTitle"));
    auto* brandSub = new QLabel(tr("نظام البيع والمحاسبة"));
    brandSub->setObjectName(QStringLiteral("appSub"));

    auto* brandTexts = new QVBoxLayout;
    brandTexts->setContentsMargins(0, 0, 0, 0);
    brandTexts->setSpacing(0);
    brandTexts->addWidget(brandTitle);
    brandTexts->addWidget(brandSub);

    auto* brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(18, 16, 18, 12);
    brandRow->setSpacing(12);
    brandRow->addWidget(brandIcon);
    brandRow->addLayout(brandTexts, 1);

    auto* about = new QPushButton(tr("حول محلي…"));
    about->setObjectName(QStringLiteral("about"));
    about->setCursor(Qt::PointingHandCursor);
    connect(about, &QPushButton::clicked, this, [this]() {
        QMessageBox::about(
            this, tr("حول محلي"),
            tr("<h3>محلي — نظام نقاط البيع والمحاسبة</h3>"
                           "<p>إدارة البيع السريع، الجرد، حسابات العملاء والموردين، "
                           "جلسات الصندوق، المصاريف، التقارير والاستردادات — "
                           "بدون اتصال وبثيَمَين فاتح/داكن.</p>"
                           "<p><b>الإصدار:</b> %1</p>")
                .arg(qApp->applicationVersion()));
    });

    auto* sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(240);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, 0, 0, 12);
    sidebarLayout->setSpacing(8);
    sidebarLayout->addLayout(brandRow);
    sidebarLayout->addWidget(m_nav, 1);
    sidebarLayout->addWidget(about, 0, Qt::AlignCenter);

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
    m_usersPage = new UsersPage(db);
    m_pages->addWidget(m_pos);
    m_products = new ProductsPage(db);
    m_pages->addWidget(m_products);
    m_customers = new CustomersPage(db);
    m_pages->addWidget(m_customers);
    m_suppliers = new SuppliersPage(db);
    m_pages->addWidget(m_suppliers);
    m_pages->addWidget(m_cashSession);
    m_pages->addWidget(m_sales);
    m_pages->addWidget(m_expenses);
    m_pages->addWidget(m_reports);
    m_pages->addWidget(m_refunds);
    m_pages->addWidget(m_auditLog);
    m_pages->addWidget(m_settings);
    m_pages->addWidget(m_usersPage);

    auto* central = new QWidget;
    // The root is vertical: the update bar, when it exists, is a banner across
    // the top, and the horizontal row below it holds the sidebar and the pages.
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(sidebar);
    bodyLayout->addWidget(m_pages, 1);
    layout->addLayout(bodyLayout);
    setCentralWidget(central);

    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::onPageChanged);

    buildNavForRole(app::core::Session::instance().currentUser().role);
    m_nav->setCurrentRow(0);

    m_statusLabel = new QLabel;
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    statusBar()->setContentsMargins(0, 0, 0, 0);
    statusBar()->setFixedHeight(36);
    statusBar()->addWidget(m_statusLabel);

    m_userLabel = new QLabel;
    m_userLabel->setObjectName(QStringLiteral("userLabel"));
    m_userLabel->setText(tr("المستخدم: %1").arg(app::core::Session::instance().actorName()));
    statusBar()->addPermanentWidget(m_userLabel);

    auto* switchUserBtn = new QPushButton(tr("تبديل المستخدم"));
    switchUserBtn->setObjectName("primary");
    switchUserBtn->setCursor(Qt::PointingHandCursor);
    switchUserBtn->setFixedHeight(32);
    connect(switchUserBtn, &QPushButton::clicked, this, &MainWindow::onSwitchUserClicked);
    statusBar()->addPermanentWidget(switchUserBtn);

    connect(&m_controller, &ServerController::statsChanged, this, &MainWindow::onSyncStatusChanged);
    onSyncStatusChanged();

    // The update check runs on a delay so it never competes with opening the
    // database, drawing the first page or whatever the user clicks first: the
    // bar is only shown if there is something to say, and a failure is silent.
    m_updateChecker = new app::core::UpdateChecker(this);
    connect(m_updateChecker, &app::core::UpdateChecker::updateAvailable,
            this, &MainWindow::onUpdateAvailable);
    connect(m_updateChecker, &app::core::UpdateChecker::upToDate,
            this, &MainWindow::onUpdateUpToDate);
    connect(m_updateChecker, &app::core::UpdateChecker::checkFailed,
            this, &MainWindow::onUpdateFailed);
    QTimer::singleShot(5000, this, [this]() { m_updateChecker->check(); });

    // Built up front but idle: the download only starts when the user asks for
    // it, and a bar is only ever shown when a newer release exists.
    m_updateDownloader = new app::core::UpdateDownloader(this);
    connect(m_updateDownloader, &app::core::UpdateDownloader::progress,
            this, &MainWindow::onUpdateDownloadProgress);
    connect(m_updateDownloader, &app::core::UpdateDownloader::finished,
            this, &MainWindow::onUpdateDownloadFinished);
    connect(m_updateDownloader, &app::core::UpdateDownloader::failed,
            this, &MainWindow::onUpdateDownloadFailed);

    QTimer::singleShot(0, this, [this]() {
        buildNavForRole(app::core::Session::instance().currentUser().role);
        m_nav->setCurrentRow(0);
    });
}

void MainWindow::onPageChanged(int row)
{
    m_pages->setCurrentIndex(row);
    switch (row) {
    case 1:
        m_products->refresh();
        break;
    case 2:
        m_customers->refresh();
        break;
    case 3:
        m_suppliers->refresh();
        break;
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
    case 11:
        m_usersPage->refresh();
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

void MainWindow::buildNavForRole(const QString& role)
{
    m_nav->clear();

    struct NavEntry {
        Icon icon;
        QString label;
        int pageIndex;
    };

    std::vector<NavEntry> entries = {
        {Icon::Cart, tr("البيع السريع"), 0},
        {Icon::Box, tr("المنتجات"), 1},
        {Icon::People, tr("العملاء"), 2},
        {Icon::Truck, tr("الموردون"), 3},
        {Icon::Wallet, tr("جلسة الصندوق"), 4},
        {Icon::Receipt, tr("مبيعات اليوم"), 5},
        {Icon::Tag, tr("المصاريف والسحوبات"), 6},
        {Icon::BarChart, tr("التقارير"), 7},
        {Icon::Return, tr("الاستردادات"), 8},
        {Icon::History, tr("سجل المراجعة"), 9},
        {Icon::Gear, tr("الإعدادات"), 10},
    };

    if (role == QStringLiteral("admin")) {
        entries.push_back({Icon::People, tr("إدارة المستخدمين"), 11});
    }

    for (const NavEntry& entry : entries) {
        auto* item = new QListWidgetItem(appIcon(entry.icon, QColor(QStringLiteral("#8b5cf6")), 20),
                                         entry.label);
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        item->setSizeHint(QSize(0, 46));
        item->setData(Qt::UserRole, entry.pageIndex);
        m_nav->addItem(item);
    }
}

void MainWindow::rebuildNav()
{
    buildNavForRole(app::core::Session::instance().currentUser().role);
}

void MainWindow::onSyncStatusChanged()
{
    const ServerController::Stats stats = m_controller.stats();
    QString text;
    if (stats.listening) {
        text = tr("خادم المزامنة: يعمل على المنفذ %1").arg(stats.port);
    } else {
        text = tr("خادم المزامنة: متوقف");
    }
    text += tr("  |  عمليات منفّذة حتى اليوم: %1").arg(stats.appliedOps);
    text += tr("  |  أجهزة متصلة: %1").arg(stats.devices);
    text += tr("  |  مبيعات اليوم: %1 (%2)")
                .arg(stats.salesToday)
                .arg(formatMoney(stats.revenueTodayCents));
    m_statusLabel->setText(text);
}

void MainWindow::onSwitchUserClicked()
{
    hide();
    app::ui::LoginDialog login(m_db);
    if (login.exec() == QDialog::Accepted) {
        m_products->refresh();
        m_customers->refresh();
        m_suppliers->refresh();
        m_cashSession->refresh();
        m_sales->refresh();
        m_expenses->refresh();
        m_reports->refresh();
        m_refunds->refresh();
        m_auditLog->refresh();
        m_settings->refresh();
        m_usersPage->refresh();
        if (m_userLabel) {
            m_userLabel->setText(tr("المستخدم: %1").arg(app::core::Session::instance().actorName()));
        }
        rebuildNav();
        show();
    } else {
        show();
    }
}

void MainWindow::onUpdateAvailable(const QString& tag, const QString& notes)
{
    Q_UNUSED(notes);
    showUpdateBar(tag);
}

void MainWindow::onUpdateUpToDate()
{
    qDebug() << "update check: up to date";
}

void MainWindow::onUpdateFailed(const QString& reason)
{
    qDebug() << "update check failed:" << reason;
}

void MainWindow::showUpdateBar(const QString& tag)
{
    if (m_updateBar) {
        // Already on screen: keep whatever the user did with it rather than
        // re-raising a bar they dismissed.
        return;
    }

    m_updateBar = new QFrame;
    m_updateBar->setObjectName(QStringLiteral("updateBar"));
    auto* barLayout = new QHBoxLayout(m_updateBar);
    barLayout->setContentsMargins(12, 8, 12, 8);
    barLayout->setSpacing(8);

    auto* label = new QLabel(tr("تحديث متاح: %1").arg(tag));
    label->setObjectName(QStringLiteral("updateBarLabel"));
    barLayout->addWidget(label, 1);

    auto* download = new QPushButton(tr("تنزيل"));
    download->setObjectName(QStringLiteral("primary"));
    download->setCursor(Qt::PointingHandCursor);
    barLayout->addWidget(download);

    auto* later = new QPushButton(tr("لاحقًا"));
    later->setObjectName(QStringLiteral("ghost"));
    later->setCursor(Qt::PointingHandCursor);
    barLayout->addWidget(later);

    // The bar's own padding is cosmetic; the gap to the window edge and to the
    // pages comes from a wrapper, since the root layout is flush by design (the
    // sidebar relies on it) and a widget cannot carry an outer margin itself.
    auto* wrapper = new QWidget;
    auto* wrapperLayout = new QVBoxLayout(wrapper);
    wrapperLayout->setContentsMargins(12, 8, 12, 8);
    wrapperLayout->setSpacing(0);
    wrapperLayout->addWidget(m_updateBar);

    // Index 0 of the vertical root: the banner sits above the sidebar and the
    // pages, rather than inside the content area where the layout was built
    // around the pages.
    qobject_cast<QVBoxLayout*>(centralWidget()->layout())->insertWidget(0, wrapper);

    // Downloading and installing is phase C: the bytes are pulled by
    // UpdateDownloader, and the restart button hands them to a batch script
    // once this process is out of the way.
    m_updateDownloadBtn = download;
    connect(download, &QPushButton::clicked, this, &MainWindow::onUpdateDownloadClicked);

    connect(later, &QPushButton::clicked, m_updateBar, [this]() {
        m_updateBar->hide();
    });
}

void MainWindow::onUpdateDownloadClicked()
{
    if (!m_updateDownloader) {
        return;
    }
    m_updateDownloadBtn->setEnabled(false);
    m_updateDownloadBtn->setText(tr("جاري التنزيل: 0%"));
    m_updateDownloader->start();
}

void MainWindow::onUpdateDownloadProgress(int percent)
{
    m_updateDownloadBtn->setText(tr("جاري التنزيل: %1%").arg(percent));
}

void MainWindow::onUpdateDownloadFinished(const QString& path)
{
    qDebug() << "update downloaded to" << path;

    if (m_updateRestartBtn) {
        return;
    }

    // The download button is hidden, not disabled, and the restart button
    // takes its slot: the download cannot be repeated by clicking the same
    // place, and the only thing left to do is restart.
    m_updateRestartBtn = new QPushButton(tr("إعادة التشغيل لتثبيت"));
    m_updateRestartBtn->setObjectName(QStringLiteral("danger"));
    m_updateRestartBtn->setCursor(Qt::PointingHandCursor);
    connect(m_updateRestartBtn, &QPushButton::clicked, this, &MainWindow::onRestartToInstall);

    auto* barLayout = qobject_cast<QHBoxLayout*>(m_updateBar->layout());
    const int slot = barLayout->indexOf(m_updateDownloadBtn);
    barLayout->insertWidget(slot, m_updateRestartBtn);
    m_updateDownloadBtn->hide();
}

void MainWindow::onUpdateDownloadFailed(const QString& reason)
{
    qDebug() << "update download failed:" << reason;
    m_updateDownloadBtn->setEnabled(true);
    m_updateDownloadBtn->setText(tr("تنزيل"));
    QMessageBox::warning(this, tr("التحديث"),
                          tr("فشل التنزيل. تحقق من الاتصال وحاول مرة أخرى."));
}

void MainWindow::onRestartToInstall()
{
#ifndef Q_OS_WIN
    // Nothing to hand the archive to: the unpack-and-replace trick is a batch
    // script, and there is no equivalent here that is safe enough to invent.
    QMessageBox::information(
        this, tr("التحديث"),
        tr("التحديث التلقائي متاح على Windows فقط. نزّل الإصدار الجديد يدويًا."));
#else
    // The archive path comes from the downloader rather than from %TEMP% or a
    // second temp lookup: one place decides where the file is, and the script
    // is told the same answer.
    const QString script = app::core::buildWindowsInstallBatch(
        QCoreApplication::applicationDirPath(),
        app::core::UpdateDownloader::defaultDestination(),
        QFileInfo(QCoreApplication::applicationFilePath()).fileName());

    // An empty script means a path that cannot be embedded safely; the builder
    // refuses rather than writing something that would copy to the wrong place.
    if (script.isEmpty()) {
        QMessageBox::warning(this, tr("التحديث"),
                              tr("تعذّر إنشاء ملف التحديث لهذا المسار."));
        return;
    }

    const QString batPath =
        QDir(QDir::tempPath()).filePath(QStringLiteral("mahali-update.bat"));
    QFile batch(batPath);
    if (!batch.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("التحديث"), tr("تعذّر إنشاء ملف التحديث."));
        return;
    }
    batch.write(script.toUtf8());
    batch.close();

    if (!QProcess::startDetached(QStringLiteral("cmd.exe"),
                                 { QStringLiteral("/c"), batPath })) {
        QMessageBox::warning(this, tr("التحديث"), tr("تعذّر بدء التحديث."));
        return;
    }

    // The script waits before touching anything, because the running executable
    // cannot be overwritten while it is loaded.
    qApp->quit();
#endif
}

} // namespace app::ui