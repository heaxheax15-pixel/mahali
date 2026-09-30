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
#include <QMessageBox>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
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
#include "data/occasion_repository.h"
#include "data/occasion_service.h"
#include "data/setting_repository.h"
#include "expenses_page.h"
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

// The page indices the sidebar and the stacked widget agree on. Grouped
// constants rather than bare numbers so a page added in the middle cannot
// silently shift the switch that refreshes a page on entry.
namespace page {
constexpr int QuickSale = 0;
constexpr int Products = 1;
constexpr int Customers = 2;
constexpr int Suppliers = 3;
constexpr int CashSession = 4;
constexpr int SalesOfDay = 5;
constexpr int Expenses = 6;
constexpr int Reports = 7;
constexpr int Refunds = 8;
constexpr int AuditLog = 9;
constexpr int Users = 10;
constexpr int Purchases = 11;
constexpr int Occasions = 12;
constexpr int Settings = 13;
} // namespace page

// A page that is on the roadmap but not written yet. It exists so its sidebar
// entry leads somewhere and says so, rather than to a button that does nothing
// when clicked.
QWidget* makeStubPage(const QString& title, const QString& body)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addStretch(1);

    auto* heading = new QLabel(title);
    heading->setObjectName(QStringLiteral("pageHeaderTitle"));
    heading->setAlignment(Qt::AlignCenter);
    auto* note = new QLabel(body);
    note->setObjectName(QStringLiteral("hint"));
    note->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);
    layout->addWidget(note);
    layout->addStretch(1);
    return page;
}

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

    m_pages = new QStackedWidget;
    m_pages->setObjectName(QStringLiteral("content"));
    // The pages are described here, not built. Each entry knows how to make its
    // page and how to ask it to re-read; ensurePage() runs the first one the
    // moment its index is opened, which for the quick sale is here, at the end
    // of this constructor. The order is the page indices the sidebar refers to,
    // and follows the page:: constants above rather than the order the buttons
    // appear in, because a button can sit anywhere in the sidebar and still open
    // a given page.
    m_pageFactories = {
        {[this] { return static_cast<QWidget*>(new PosPage(m_db)); },
         // The quick sale never re-read on entry before this change either: it
         // keeps its own totals current as things are sold. Left as it was.
         nullptr},
        {[this] { return static_cast<QWidget*>(new ProductsPage(m_db)); },
         [](QWidget* w) { qobject_cast<ProductsPage*>(w)->refresh(); }},
        {[this] { return static_cast<QWidget*>(new CustomersPage(m_db)); },
         [](QWidget* w) { qobject_cast<CustomersPage*>(w)->refresh(); }},
        {[this] { return static_cast<QWidget*>(new SuppliersPage(m_db)); },
         [](QWidget* w) { qobject_cast<SuppliersPage*>(w)->refresh(); }},
        {[this] { return static_cast<QWidget*>(new CashSessionPage(m_db)); },
         [](QWidget* w) { qobject_cast<CashSessionPage*>(w)->refresh(); }},
        {[this] { return static_cast<QWidget*>(new SalesPage(m_db)); },
         [](QWidget* w) { qobject_cast<SalesPage*>(w)->refresh(); }},
        {[this] { return static_cast<QWidget*>(new ExpensesPage(m_db)); },
         [](QWidget* w) { qobject_cast<ExpensesPage*>(w)->refresh(); }},
        {[this] { return static_cast<QWidget*>(new ReportsPage(m_db)); },
         [](QWidget* w) { qobject_cast<ReportsPage*>(w)->refresh(); }},
        {[this] { return static_cast<QWidget*>(new RefundsPage(m_db)); },
         [](QWidget* w) { qobject_cast<RefundsPage*>(w)->refresh(); }},
        {[this] { return static_cast<QWidget*>(new AuditLogPage(m_db)); },
         [](QWidget* w) { qobject_cast<AuditLogPage*>(w)->refresh(); }},
        {[this] { return static_cast<QWidget*>(new UsersPage(m_db)); },
         [](QWidget* w) { qobject_cast<UsersPage*>(w)->refresh(); }},
        // Two placeholders so the navigation has somewhere to land for the pages
        // that do not exist yet. They are real pages in the stack, so clicking the
        // entry shows an honest "not built yet" instead of doing nothing. Both are
        // four widgets, so they are built like the rest rather than kept eager.
        {[this] { return makeStubPage(tr("Achats"), tr("Page en construction.")); },
         nullptr},
        {[this] { return makeStubPage(tr("Occasions"), tr("Page en construction.")); },
         nullptr},
        {[this] {
             auto* page = new SettingsPage(m_db);
             // The settings page can change the theme without going through the
             // toggle, so the icons have to be told about that route too. Wired
             // here rather than in the constructor because the page does not
             // exist until it is opened.
             connect(page, &SettingsPage::themeChanged, this, &MainWindow::refreshThemeIcons);
             return static_cast<QWidget*>(page);
         },
         [](QWidget* w) { qobject_cast<SettingsPage*>(w)->refresh(); }},
    };

    // An empty widget per index until the page is opened. The stack keeps the
    // count and the order from here on, so every index the sidebar, the fade and
    // the nav highlight use is already correct before the first page exists.
    for (int i = 0; i < m_pageFactories.size(); ++i) {
        m_pages->addWidget(new QWidget);
    }

    // Index guards rather than trust: the stack and the page:: constants above
    // are written side by side, and a mismatch should fail loudly here rather
    // than show the wrong page to someone trying to take money.
    Q_ASSERT(m_pages->count() == page::Settings + 1);
    Q_ASSERT(m_pageFactories.size() == page::Settings + 1);

    auto* central = new QWidget;
    // The root is vertical: the top bar spans the full width, the update bar
    // sits under it as a banner when there is something to say, and the
    // horizontal row below holds the sidebar and the pages.
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildTopBar());

    auto* bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(buildSidebar());
    bodyLayout->addWidget(m_pages, 1);
    layout->addLayout(bodyLayout);
    setCentralWidget(central);

    buildNavForRole(app::core::Session::instance().currentUser().role);

    // The bar carries the one action that has to be reachable from anywhere.
    // The sync and tally readouts that used to sit here were secondary, and a
    // bar of four counters is a bar nobody reads.
    statusBar()->setContentsMargins(0, 0, 0, 0);
    statusBar()->setFixedHeight(36);

    auto* switchUserBtn = new QPushButton(tr("تبديل المستخدم"));
    switchUserBtn->setObjectName("primary");
    switchUserBtn->setCursor(Qt::PointingHandCursor);
    switchUserBtn->setFixedHeight(32);
    connect(switchUserBtn, &QPushButton::clicked, this, &MainWindow::onSwitchUserClicked);
    statusBar()->addPermanentWidget(switchUserBtn);

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

    // Opened on the quick sale: the first click an operator makes is almost
    // always a sale, and the page should be ready before they look for it.
    onPageChanged(page::QuickSale);
}

// Builds the page at this index the first time it is asked for, and puts it in
// the slot the placeholder was sitting in, so the index keeps meaning the same
// page to the sidebar, the nav highlight and the fade.
QWidget* MainWindow::ensurePage(int row)
{
    if (row < 0 || row >= m_pageFactories.size()) {
        return nullptr;
    }
    PageFactory& factory = m_pageFactories[row];
    if (factory.instance != nullptr) {
        return factory.instance;
    }

    QWidget* page = factory.create();
    if (page == nullptr) {
        return nullptr;
    }
    // The placeholder is ours and holds nothing, so it goes now rather than at
    // the end of the window's life: leaving fourteen of them behind would keep
    // the widget count climbing with every page opened.
    QWidget* placeholder = m_pages->widget(row);
    m_pages->removeWidget(placeholder);
    delete placeholder;
    m_pages->insertWidget(row, page);
    factory.instance = page;
    return page;
}

void MainWindow::refreshPage(int row)
{
    if (row < 0 || row >= m_pageFactories.size()) {
        return;
    }
    const PageFactory& factory = m_pageFactories[row];
    // Nothing to correct on a page nobody has opened, and no entry for the ones
    // that never re-read on entry.
    if (factory.instance == nullptr || !factory.refresh) {
        return;
    }
    factory.refresh(factory.instance);
}

PosPage* MainWindow::posPage()
{
    return qobject_cast<PosPage*>(ensurePage(page::QuickSale));
}

CashSessionPage* MainWindow::cashSessionPage()
{
    return qobject_cast<CashSessionPage*>(ensurePage(page::CashSession));
}

SalesPage* MainWindow::salesPage()
{
    return qobject_cast<SalesPage*>(ensurePage(page::SalesOfDay));
}

ExpensesPage* MainWindow::expensesPage()
{
    return qobject_cast<ExpensesPage*>(ensurePage(page::Expenses));
}

ReportsPage* MainWindow::reportsPage()
{
    return qobject_cast<ReportsPage*>(ensurePage(page::Reports));
}

SettingsPage* MainWindow::settingsPage()
{
    return qobject_cast<SettingsPage*>(ensurePage(page::Settings));
}

RefundsPage* MainWindow::refundsPage()
{
    return qobject_cast<RefundsPage*>(ensurePage(page::Refunds));
}

AuditLogPage* MainWindow::auditLogPage()
{
    return qobject_cast<AuditLogPage*>(ensurePage(page::AuditLog));
}

UsersPage* MainWindow::usersPage()
{
    return qobject_cast<UsersPage*>(ensurePage(page::Users));
}

void MainWindow::onPageChanged(int row)
{
    // The page is built before it is shown, so the operator never sees a
    // placeholder, and the re-read below lands on the widget now in the stack.
    ensurePage(row);
    m_pages->setCurrentIndex(row);
    // Each page re-reads on entry, so what the operator sees is what is in the
    // database now rather than what it held when the window opened.
    refreshPage(row);

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
    // The stacked widget is what actually moved, so the highlight follows it
    // rather than the click that asked for the change.
    refreshNavActiveState();
}

// The strip above everything: the brand on the right, then the shop's global
// search, then the controls that used to sit in the status bar (who is signed
// in, which occasion is running) plus the theme toggle.
QWidget* MainWindow::buildTopBar()
{
    m_topBar = new QWidget;
    m_topBar->setObjectName(QStringLiteral("topBar"));
    m_topBar->setFixedHeight(56);

    m_brandIcon = new QLabel;
    m_brandIcon->setObjectName(QStringLiteral("brandIcon"));
    m_brandIcon->setFixedSize(36, 36);
    m_brandIcon->setAlignment(Qt::AlignCenter);

    auto* brandTitle = new QLabel(tr("Mahali"));
    brandTitle->setObjectName(QStringLiteral("appTitle"));

    auto* brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(0, 0, 0, 0);
    brandRow->setSpacing(10);
    brandRow->addWidget(m_brandIcon);
    brandRow->addWidget(brandTitle);

    m_occasionLabel = new QLabel;
    m_occasionLabel->setObjectName(QStringLiteral("occasionLabel"));
    // Filled now so a shop that opens mid-occasion says so before the operator
    // has done anything. Refreshed again on activate/deactivate.
    refreshOccasionLabel();

    // Prefix and name in one label: a single string is bidi-correct as a whole,
    // whereas two labels let the layout put the name on the wrong side of the
    // colon under a right-to-left direction.
    m_userLabel = new QLabel;
    m_userLabel->setObjectName(QStringLiteral("userLabel"));
    m_userLabel->setText(tr("المستخدم: %1").arg(app::core::Session::instance().actorName()));

    m_themeToggle = new QPushButton;
    m_themeToggle->setObjectName(QStringLiteral("iconButton"));
    m_themeToggle->setFixedSize(36, 36);
    m_themeToggle->setCursor(Qt::PointingHandCursor);
    m_themeToggle->setToolTip(tr("تبديل السمة"));
    connect(m_themeToggle, &QPushButton::clicked, this, &MainWindow::onThemeToggleClicked);

    m_settingsBtn = new QPushButton;
    m_settingsBtn->setObjectName(QStringLiteral("iconButton"));
    m_settingsBtn->setFixedSize(36, 36);
    m_settingsBtn->setCursor(Qt::PointingHandCursor);
    m_settingsBtn->setToolTip(tr("الإعدادات"));
    // Characters, not painted pixmaps. The stroked icons were unreadable at this
    // size, and these three marks were checked against the app's font: each one
    // lands as a filled shape, not a missing-glyph box.
    m_settingsBtn->setText(QString::fromUtf8("\xe2\x9a\x99"));
    connect(m_settingsBtn, &QPushButton::clicked, this,
            [this]() { onPageChanged(page::Settings); });

    // Drawn once the widgets exist, so the first paint is already the right
    // colour rather than a flash of the light-theme one.
    refreshThemeIcons();

    // In RTL the first widget added lands on the right. The brand leads, the
    // occasion badge sits beside it, and the user and the two icon buttons close
    // the left end. The stretch takes up what is left, which is what holds the
    // two groups apart now that the bar carries no search field.
    auto* layout = new QHBoxLayout(m_topBar);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(12);
    layout->addLayout(brandRow);
    layout->addSpacing(16);
    layout->addWidget(m_occasionLabel);
    layout->addStretch(1);
    layout->addWidget(m_userLabel);
    layout->addWidget(m_themeToggle);
    layout->addWidget(m_settingsBtn);
    return m_topBar;
}

// The right-hand rail of pages, split into the groups an operator thinks in:
// selling, the catalogue, money, then the administrative pages. The brand and
// the "about" button are gone from here — they belong in the top bar.
QWidget* MainWindow::buildSidebar()
{
    m_sidebar = new QWidget;
    m_sidebar->setObjectName(QStringLiteral("sidebar"));
    m_sidebar->setFixedWidth(240);

    auto* layout = new QVBoxLayout(m_sidebar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_sidebarGroupLayout = layout;

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

    // The groups stack from the top; the slack before the footer pushes "about"
    // to the foot of the rail rather than letting it climb up under the groups.
    layout->addStretch(1);
    layout->addWidget(about, 0, Qt::AlignHCenter);
    return m_sidebar;
}

void MainWindow::addNavGroup(const QString& title, const std::vector<NavEntry>& entries)
{
    auto* group = new QWidget;
    group->setObjectName(QStringLiteral("navGroup"));
    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(0, 14, 0, 0);
    groupLayout->setSpacing(2);

    auto* heading = new QLabel(title);
    heading->setObjectName(QStringLiteral("navGroupTitle"));
    groupLayout->addWidget(heading);

    for (const NavEntry& entry : entries) {
        auto* button = new QPushButton(entry.label);
        button->setObjectName(QStringLiteral("navItem"));
        button->setCursor(Qt::PointingHandCursor);
        // The rail is a fixed 240px and the button fills what the group gives
        // it. Spelled out rather than left to the default (Minimum, Fixed),
        // because a minimum-based policy is what lets a layout shrink a label
        // below the text it is holding.
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setCheckable(true);
        // The label is Arabic, so it reads right-to-left; the icon then belongs
        // on the right of it rather than wherever the button puts it.
        button->setLayoutDirection(Qt::RightToLeft);
        // The icon is only a placeholder here. The colour is not known for
        // certain until the button knows whether it is the active one, and the
        // theme may change while the window is open, so the first real colour
        // comes from refreshThemeIcons, which is called once the groups are all
        // built and again on every theme change.
        button->setProperty("active", false);

        const int pageIndex = entry.pageIndex;
        connect(button, &QPushButton::clicked, this, [this, pageIndex]() {
            onPageChanged(pageIndex);
        });

        m_navButtons.push_back({button, pageIndex});
        m_navIcons.push_back(entry.icon);
        groupLayout->addWidget(button);
    }

    m_sidebarGroupLayout->addWidget(group);
}

void MainWindow::buildNavForRole(const QString& role)
{
    // Rebuilt from scratch when the signed-in role changes, so a group that no
    // longer applies does not leave its buttons behind.
    m_navButtons.clear();
    m_navIcons.clear();
    // Empty the group column first: deleting the group widgets leaves their
    // layout items behind, and a second call would stack a fresh set of groups
    // on top of the old ones.
    while (QLayoutItem* item = m_sidebarGroupLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            delete widget;
        }
        delete item;
    }

    addNavGroup(tr("نقطة البيع"), {
                           {Icon::Cart, tr("البيع السريع"), page::QuickSale},
                           {Icon::Wallet, tr("جلسة الصندوق"), page::CashSession},
                           {Icon::Receipt, tr("مبيعات اليوم"), page::SalesOfDay},
                       });

    addNavGroup(tr("الإدارة"), {
                           {Icon::Box, tr("المنتجات"), page::Products},
                           {Icon::People, tr("العملاء"), page::Customers},
                           {Icon::Truck, tr("الموردون"), page::Suppliers},
                           {Icon::Truck, tr("Achats"), page::Purchases},
                           {Icon::Sun, tr("المناسبات"), page::Occasions},
                       });

    addNavGroup(tr("المالية"), {
                           {Icon::Tag, tr("المصاريف والسحوبات"), page::Expenses},
                           {Icon::BarChart, tr("التقارير"), page::Reports},
                           {Icon::Return, tr("الاستردادات"), page::Refunds},
                       });

    std::vector<NavEntry> system = {
        {Icon::History, tr("سجل المراجعة"), page::AuditLog},
    };
    if (role == QStringLiteral("admin")) {
        // User management leads the group, and it is only here for an admin, so
        // it is built into the vector rather than appended after the log.
        system.insert(system.begin(),
                      NavEntry{Icon::People, tr("إدارة المستخدمين"), page::Users});
    }
    addNavGroup(tr("النظام"), system);

    refreshNavActiveState();
}

void MainWindow::refreshNavActiveState()
{
    const int current = m_pages->currentIndex();
    for (const NavButton& entry : m_navButtons) {
        const bool active = entry.pageIndex == current;
        if (entry.button->isChecked() == active) {
            continue;
        }
        // setProperty alone does not restyle the button: the style has to be
        // unpolished and polished again for the [active] rule to be re-read.
        entry.button->setChecked(active);
        entry.button->setProperty("active", active);
        // A dynamic property only re-reads the stylesheet once the style is
        // cleared, so unpolish/polish is what actually repaints the button.
        entry.button->style()->unpolish(entry.button);
        entry.button->style()->polish(entry.button);
        entry.button->update();
    }

    // The icon colour is tied to the same state, and it is a pixmap the
    // stylesheet cannot reach, so it is redrawn from here rather than by the
    // unpolish above.
    refreshThemeIcons();
}

// Every icon the shell draws: the top bar's brand, settings and theme controls,
// and the sidebar's nav buttons. A stylesheet sets a foreground colour rather
// than recolouring a pixmap, so it cannot reach any of them; they are drawn here
// instead, and every change of theme — from the toggle or from the settings
// page's selector — comes back through this one function.
void MainWindow::refreshThemeIcons()
{
    // The theme in use, not the one in the database: this has to match what
    // applyTheme actually put on screen. Reading the stored setting instead would
    // leave the icons on the old theme whenever the change has not been saved.
    const bool dark = activeTheme() == QStringLiteral("dark");
    const QColor accent(dark ? QStringLiteral("#d4a017") : QStringLiteral("#2563eb"));
    const QColor text(dark ? QStringLiteral("#fafafa") : QStringLiteral("#0f172a"));
    const QColor muted(dark ? QStringLiteral("#a3a3a3") : QStringLiteral("#64748b"));

    if (m_brandIcon) {
        // A letter, not the storefront pictogram: the drawn mark read as loose
        // strokes at this size, whereas one heavy glyph is unambiguous. The
        // colour is set here because the accent is a theme value that a
        // stylesheet cannot reach without being rebuilt on every switch.
        m_brandIcon->setText(QStringLiteral("M"));
        m_brandIcon->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 24px; font-weight: 900; }")
                                       .arg(accent.name()));
    }
    if (m_settingsBtn) {
        m_settingsBtn->setText(QString::fromUtf8("\xe2\x9a\x99"));
    }
    if (m_themeToggle) {
        m_themeToggle->setText(dark ? QString::fromUtf8("\xe2\x98\x80")
                                    : QString::fromUtf8("\xf0\x9f\x8c\x99"));
    }

    // The nav icons follow the button they sit on: the accent on the filled
    // active button, the plain text colour everywhere else. The icon size is set
    // here too, because the buttons are created without one and an unset
    // setIconSize leaves Qt guessing from the pixmap.
    const int current = m_pages->currentIndex();
    for (qsizetype i = 0; i < m_navIcons.size() && i < m_navButtons.size(); ++i) {
        const NavButton& entry = m_navButtons[static_cast<std::size_t>(i)];
        if (!entry.button) {
            continue;
        }
        const bool active = entry.pageIndex == current;
        entry.button->setIconSize(QSize(20, 20));
        entry.button->setIcon(appIcon(m_navIcons[static_cast<qsizetype>(i)],
                                      active ? accent : text,
                                      20));
    }
}

void MainWindow::onThemeToggleClicked()
{
    const QString next = activeTheme() == QStringLiteral("dark") ? QStringLiteral("light")
                                                                : QStringLiteral("dark");
    applyTheme(next, *qApp);
    // Stored so the choice survives a restart; the settings page's own selector
    // reads the same key, so the two cannot disagree.
    data::SettingRepository settings(m_db);
    settings.set(QStringLiteral("theme"), next);
    // The stylesheet has already been swapped; this redraws the pixmaps, which
    // the new stylesheet could not have reached on its own.
    refreshThemeIcons();
}

void MainWindow::rebuildNav()
{
    buildNavForRole(app::core::Session::instance().currentUser().role);
}


// Reads the running occasion and shows its name, or clears the label when there
// is none. Built on every change rather than tracked by hand, so the bar cannot
// disagree with the setting.
void MainWindow::refreshOccasionLabel()
{
    if (!m_occasionLabel) {
        return;
    }

    data::OccasionRepository occasions(m_db);
    data::SettingRepository settings(m_db);
    data::OccasionService service(m_db, occasions, settings);

    if (const std::optional<core::Occasion> occasion = service.current()) {
        // The icon is optional, so the label is built either way and the name is
        // appended only when there is an icon to show.
        QString text = occasion->icon.isEmpty() ? QString() : occasion->icon + QStringLiteral(" ");
        text += occasion->name;
        m_occasionLabel->setText(text);
    } else {
        m_occasionLabel->clear();
    }
}

QString MainWindow::occasionLabelText() const
{
    return m_occasionLabel ? m_occasionLabel->text() : QString();
}

bool MainWindow::activateOccasion(int occasionId)
{
    data::OccasionRepository occasions(m_db);
    data::SettingRepository settings(m_db);
    data::OccasionService service(m_db, occasions, settings);

    const bool ok = service.activate(occasionId);
    // Refreshed either way: a refused activation leaves the bar showing what was
    // already running, which is worth confirming rather than assuming.
    refreshOccasionLabel();
    return ok;
}

void MainWindow::deactivateOccasion()
{
    data::OccasionRepository occasions(m_db);
    data::SettingRepository settings(m_db);
    data::OccasionService service(m_db, occasions, settings);

    service.deactivate();
    refreshOccasionLabel();
}

void MainWindow::onSwitchUserClicked()
{
    hide();
    app::ui::LoginDialog login(m_db);
    if (login.exec() == QDialog::Accepted) {
        // Only the pages that have been opened: a page behind a placeholder has
        // never drawn anything, and building all fourteen to re-read them is the
        // wait this change exists to remove. It reads on the way in anyway.
        for (int row = 0; row < m_pageFactories.size(); ++row) {
            refreshPage(row);
        }
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