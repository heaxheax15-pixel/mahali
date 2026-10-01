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
#include <QShortcut>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <functional>
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
#include "stock_page.h"
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
// Appended at the end, never in the middle. These constants are also the stacked
// widget's indices, and the sidebar, the fade and the quick-nav buttons all hold
// an index of their own; slipping a page in between them would silently repoint
// every button built after it at the wrong page.
constexpr int Stock = 14;
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

// One of the four square buttons in the top bar: the page's icon over its name.
//
// A QPushButton with a layout inside, not a QToolButton with
// ToolButtonTextUnderIcon. The mode is what the two agree on, but the stylesheet
// settles it: the rules are written as QPushButton#quickNavButton, and Qt matches
// a QSS type selector against the widget's own class rather than its base classes,
// so a QToolButton would have missed every one of them and kept the theme's plain
// button look with no warning. The icon is a QLabel for the same reason the text
// is: QPushButton has no icon-over-text mode, and its own text cannot be styled
// separately from the button.
//
// Both labels are transparent to the mouse so the click lands on the button. Left
// out, they would swallow it and the button would answer only in the few pixels of
// padding around them.
QPushButton* makeQuickNavButton(const QString& label)
{
    auto* button = new QPushButton;
    button->setObjectName(QStringLiteral("quickNavButton"));
    button->setFixedSize(64, 64);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(label);
    // Written before the first polish: the [active="true"] rule is matched when
    // the widget is first styled, so leaving the property unset would paint the
    // button in the default colours until the first page change came along.
    button->setProperty("active", false);

    auto* layout = new QVBoxLayout(button);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->setAlignment(Qt::AlignCenter);

    auto* icon = new QLabel(button);
    icon->setObjectName(QStringLiteral("quickNavIcon"));
    // Pinned so the pixmap cannot stretch the button past its 64px and the text
    // below it cannot be squeezed out when the label is long.
    icon->setFixedSize(24, 24);
    icon->setAlignment(Qt::AlignCenter);
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* text = new QLabel(label, button);
    text->setObjectName(QStringLiteral("quickNavLabel"));
    text->setAlignment(Qt::AlignCenter);
    text->setAttribute(Qt::WA_TransparentForMouseEvents);

    layout->addWidget(icon, 0, Qt::AlignCenter);
    layout->addWidget(text);
    return button;
}

// Qt's stylesheet has no pseudo-state for reading direction, so the direction
// has to reach the stylesheet as an ordinary property it can match on. The
// token is the direction the application is in right now, which core's
// applyLanguage() has already pinned by the time any of this is built.
QString directionToken()
{
    return QApplication::layoutDirection() == Qt::RightToLeft ? QStringLiteral("rtl")
                                                              : QStringLiteral("ltr");
}

// Watches the window for a translator being installed. When the language
// changes, the reading direction changes with it, and a property only written at
// construction would leave the stylesheet matching a token that is no longer
// true: the sidebar rule and the active nav marker would keep drawing on the
// edge that used to be the leading one.
//
// This lives here rather than as an event() override on the window because that
// would mean editing the header, and installing a filter on self is the same
// thing as far as the stylesheet is concerned. The watcher holds no state of its
// own and declares no signals or slots, so it needs no moc entry.
class DirectionWatcher : public QObject {
public:
    explicit DirectionWatcher(std::function<void()> onLanguageChange)
        : m_callback(std::move(onLanguageChange))
    {
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::LanguageChange && m_callback) {
            m_callback();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_callback;
};

} // namespace

MainWindow::MainWindow(app::data::Database& db, ServerController& controller, QWidget* parent)
    : QMainWindow(parent)
    , m_db(db)
    , m_controller(controller)
{
    setWindowTitle(tr("محلي — نظام نقاط البيع والمحاسبة"));

    // The reading direction is not set here on purpose. core::applyLanguage()
    // pins it on the QGuiApplication, once, from the stored language, and this
    // window inherits it. Pinning it here would outrank that: Qt resolves
    // direction down the widget tree, and this widget is a descendant of the
    // application, so a direction set here silently overrode the language for
    // every page below it no matter what the user had chosen.

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
        // The stock page is one of the four the top bar puts a square button for.
        {[this] { return static_cast<QWidget*>(new StockPage(m_db)); },
         [](QWidget* w) { qobject_cast<StockPage*>(w)->refresh(); }},
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
    Q_ASSERT(m_pages->count() == page::Stock + 1);
    Q_ASSERT(m_pageFactories.size() == page::Stock + 1);

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

    // The rail is shown or hidden from what was stored, and read here rather than
    // in buildSidebar() because the button that changes it is built earlier, in
    // buildTopBar(), while the rail itself only exists once buildSidebar() has
    // run above. Reading it inside the rail's own builder would have meant the
    // two halves of one decision living in two places.
    //
    // An absent key reads as shown: a window that has never been toggled opens
    // with its navigation, which is what a first run should look like.
    {
        const auto stored = data::SettingRepository(m_db).value(QStringLiteral("sidebar_visible"));
        m_sidebarVisible = !stored.has_value() || *stored != QStringLiteral("0");
        m_sidebar->setVisible(m_sidebarVisible);
    }

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

    // F11 fills the screen and comes back out of it. A shortcut rather than a
    // keyPressEvent override: the override only ever sees a key that no focused
    // widget has already taken, and the scan field takes every one of them, so
    // F11 would have been dead exactly when the cashier is most likely to press
    // it. ApplicationShortcut rather than WindowShortcut because the point is to
    // answer regardless of which child holds the focus, the dialogs included.
    auto* fullScreen = new QShortcut(QKeySequence(Qt::Key_F11), this);
    fullScreen->setContext(Qt::ApplicationShortcut);
    connect(fullScreen, &QShortcut::activated, this, [this]() {
        if (isFullScreen()) {
            showNormal();
        } else {
            showFullScreen();
        }
    });

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

    // The register takes the caret back every time it is the page on screen. A
    // page that is switched away and back leaves the focus wherever the last
    // click put it, so without this a cashier returning to the register after
    // touching a table would be typing a barcode into a cell.
    //
    // Asked of the page the stack is now holding rather than of posPage(), which
    // would build the register just because some other page was opened.
    if (row == page::QuickSale) {
        if (auto* pos = qobject_cast<PosPage*>(m_pages->currentWidget())) {
            pos->focusEntry();
        }
    }
}

void MainWindow::onSidebarToggleClicked()
{
    m_sidebarVisible = !m_sidebarVisible;
    // No animation. The rail takes a fixed 240px from the body's row layout, so a
    // slide would mean animating the layout as well as the widget, and the tables
    // would have to be reflowed at every frame of it to stay aligned. Hiding the
    // widget is enough on its own: a layout skips hidden widgets, so the page's
    // stretch takes the rail's width and the tables resize with it.
    m_sidebar->setVisible(m_sidebarVisible);
    // Written so the window opens as it was left. A shop that works with the rail
    // closed gets it back closed, rather than being shown a rail on every start.
    data::SettingRepository(m_db).set(QStringLiteral("sidebar_visible"),
                                      m_sidebarVisible ? QStringLiteral("1")
                                                       : QStringLiteral("0"));
}

// The strip above everything: the brand on the right, the rail's toggle, the four
// reached most often, the shop's global search, then the controls that used to
// sit in the status bar (who is signed in, which occasion is running) plus the
// theme toggle.
QWidget* MainWindow::buildTopBar()
{
    m_topBar = new QWidget;
    m_topBar->setObjectName(QStringLiteral("topBar"));
    // Was 56, which fitted the 36px controls it held. The four square buttons are
    // 64 tall, and a fixed height does not grow to fit a child: at 56 the button
    // row was clipped and the names under the icons were cut off.
    m_topBar->setFixedHeight(80);

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

    // The rail's own show/hide. Placed right after the brand rather than at either
    // end of the bar, because that is the one position that puts it on the same
    // edge as the rail in both languages: the rail follows the reading direction,
    // and so does the layout order. In Arabic the bar fills right to left and this
    // lands next to the rail; in French it fills left to right and it lands there
    // too. At either end of the layout it would sit opposite the rail in one of the
    // two, which is where a control for something is least expected.
    m_sidebarToggle = new QPushButton;
    m_sidebarToggle->setObjectName(QStringLiteral("sidebarToggle"));
    // A character rather than a drawn pixmap, like the two controls beside it: the
    // stylesheet sets a foreground colour and so can reach this one, which it
    // cannot do for a pixmap. U+2630 is in the basic multilingual plane, so it
    // needs no UTF-8 decoding to survive into the QString.
    m_sidebarToggle->setText(QString(QChar(0x2630)));
    m_sidebarToggle->setFixedSize(40, 40);
    m_sidebarToggle->setCursor(Qt::PointingHandCursor);
    // One tooltip for both states rather than one that tracks the toggle: the mark
    // is a hamburger either way, and a tooltip that changed meaning on every click
    // would have to be rewritten in step with the state.
    m_sidebarToggle->setToolTip(tr("إظهار/إخفاء القائمة"));
    connect(m_sidebarToggle, &QPushButton::clicked, this, &MainWindow::onSidebarToggleClicked);

    // The four pages an operator reaches for most, next to the brand. The same
    // pages the sidebar lists, put where the eye already is: the sidebar is a
    // rail of fourteen items that has to be scrolled, and the quick sale is the
    // one page that must never be more than one click away.
    auto* quickNav = new QWidget;
    quickNav->setObjectName(QStringLiteral("quickNav"));
    auto* quickNavLayout = new QHBoxLayout(quickNav);
    quickNavLayout->setContentsMargins(0, 0, 0, 0);
    // 7px is the gap between the four, chosen so the row reads as one block
    // rather than as four separate controls.
    quickNavLayout->setSpacing(7);

    struct QuickNavEntry {
        Icon icon;
        QString label;
        int pageIndex;
    };
    // Box for the stock page, not a warehouse: there is no warehouse pictogram in
    // app_icon.h, and drawing one would mean editing the icon set, which is out of
    // this change's scope. Box is what a stock page shows anyway, so Produits and
    // Stock share an icon until a real one is added.
    const std::vector<QuickNavEntry> quickEntries = {
        {Icon::Cart, tr("البيع السريع"), page::QuickSale},
        {Icon::Box, tr("المنتجات"), page::Products},
        {Icon::Box, tr("المخزون"), page::Stock},
        {Icon::BarChart, tr("التقارير"), page::Reports},
    };
    for (const QuickNavEntry& entry : quickEntries) {
        QPushButton* button = makeQuickNavButton(entry.label);
        quickNavLayout->addWidget(button);
        const int pageIndex = entry.pageIndex;
        connect(button, &QPushButton::clicked, this, [this, pageIndex]() {
            onPageChanged(pageIndex);
        });
        // Held with its icon, like the sidebar's: the highlight has to be
        // repainted from the page on screen and the pixmap has to be redrawn on a
        // theme change, and neither can be done from a button the window no
        // longer holds.
        m_quickNavButtons.push_back({button, pageIndex});
        m_quickNavIcons.push_back(entry.icon);
    }

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

    // In RTL the first widget added lands on the right. The brand leads, the rail's
    // toggle sits against it, the quick pages follow where a second click is never
    // needed, the occasion badge after them, and the user and the two icon buttons
    // close the left end. The stretch takes up what is left, which is what holds
    // the two groups apart now that the bar carries no search field.
    auto* layout = new QHBoxLayout(m_topBar);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(12);
    layout->addLayout(brandRow);
    layout->addWidget(m_sidebarToggle);
    layout->addSpacing(16);
    layout->addWidget(quickNav);
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
    // Qt does not draw a stylesheet background or border on a plain QWidget: it
    // leaves the widget with whatever the palette gives it, which is the window
    // colour. The rail was therefore showing #efefef in both themes instead of
    // the #f1f5f9 and #111111 the theme asks for, and the rule separating it
    // from the page was not drawn at all. Asking to be styled is what makes the
    // sidebar rules below apply; without it the direction rules are inert too.
    m_sidebar->setAttribute(Qt::WA_StyledBackground, true);
    // The rail carries a rule on its inner edge to separate it from the page, and
    // which edge that is depends on the reading direction: the rail sits on the
    // right in Arabic and on the left in French, so the rule has to follow it.
    // The stylesheet matches this token, because it has no direction of its own to
    // ask.
    m_sidebar->setProperty("direction", directionToken());
    m_sidebar->setFixedWidth(240);

    auto* layout = new QVBoxLayout(m_sidebar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // The groups live in a scroll area, not directly in the rail. The thirteen
    // items and five headings need about 920px, which is more than the window's
    // 600px minimum height, so without this the last groups are simply cut off
    // and the "about" button is pushed out of sight. The content is laid out
    // resizably in the rail's own fixed width and scrolls on the vertical axis
    // only: a horizontal bar here would shrink the items and squeeze the
    // longest label.
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("navScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // The viewport is a plain child widget, so it needs naming of its own: left
    // to the default it would paint the window's own background over the rail's.
    scroll->viewport()->setObjectName(QStringLiteral("navScrollViewport"));

    // Only the groups go in here. The group column used to be the rail's own
    // layout, which meant emptying it for a role change also destroyed the
    // "about" button and the stretch above it, taking "حول محلي…" off the rail
    // for good. Keeping the groups in their own widget makes the clear in
    // buildNavForRole reach the groups and nothing else.
    auto* content = new QWidget;
    content->setObjectName(QStringLiteral("navScrollContent"));
    m_sidebarGroupLayout = new QVBoxLayout(content);
    m_sidebarGroupLayout->setContentsMargins(0, 0, 0, 0);
    m_sidebarGroupLayout->setSpacing(0);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    // The direction token on the rail and the nav buttons is written when they
    // are built, which is after applyLanguage() has run, so it is right from the
    // first paint. A language picked later in the session is a different story:
    // applyLanguage() installs a translator, Qt answers that with a
    // LanguageChange event, and this is where the token is rewritten. The lambda
    // stands in for a member function so the header, which is outside this
    // change's scope, does not have to grow a declaration for it.
    //
    // Only widgets whose token actually changed are re-polished. A widget is
    // skipped when the token comes back the same, which is the case whenever the
    // language moved within one direction -- French to English, say -- and
    // unpolishing the whole rail to redraw an identical stylesheet is wasted
    // work. The property has to be set before the polish, since the stylesheet
    // reads it while it is matching.
    auto* watcher = new DirectionWatcher([this]() {
        const QString token = directionToken();
        const auto retag = [token](QWidget* widget) {
            if (!widget || widget->property("direction").toString() == token) {
                return;
            }
            widget->setProperty("direction", token);
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
        };
        retag(m_sidebar);
        for (const NavButton& nav : m_navButtons) {
            retag(nav.button);
        }
    });
    watcher->installEventFilter(this);
    watcher->setParent(this);

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

    // The scroll area carries the slack, so "about" sits at the foot of the
    // rail on its own rather than climbing up under the groups.
    layout->addWidget(about, 0, Qt::AlignHCenter);
    return m_sidebar;
}

void MainWindow::addNavGroup(const QString& title, const std::vector<NavEntry>& entries)
{
    auto* group = new QWidget;
    group->setObjectName(QStringLiteral("navGroup"));
    auto* groupLayout = new QVBoxLayout(group);
    // The 21px that opens a group is the heading's own top padding, so it sits
    // in the same place for the first group as for the rest — one number, in
    // one theme rule, instead of a margin here that drifts from the stylesheet.
    // The 2px between items is a layout fact, so it is set here rather than in
    // the stylesheet; QSS margin on a child widget does not add to a layout's
    // spacing, and putting it in the stylesheet only gave a number that looked
    // like it controlled the gap but did not.
    groupLayout->setContentsMargins(0, 0, 0, 0);
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
        // The active item is marked with a rule on the leading edge, and the
        // leading edge is the right in Arabic and the left in French. The token
        // is what lets the stylesheet put the rule, and the padding that clears
        // it, on the right side of the button for each.
        button->setProperty("direction", directionToken());
        // No setLayoutDirection here either: the button takes the direction from
        // the window, which takes it from the language. Forcing RTL on the
        // button alone was what put the icon on the right of the label in the
        // Arabic UI, and would have kept doing so in a French one.
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
    // The sidebar rail and the top bar's four squares, through the same code: both
    // mark the page on screen rather than the page that was asked for, so a
    // programmatic switch lights up both and they cannot disagree about which one
    // is showing.
    for (const std::vector<NavButton>* row : {&m_navButtons, &m_quickNavButtons}) {
        for (const NavButton& entry : *row) {
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
    // Indexed by std::size_t and not by qsizetype: the button list is a std::vector
    // and the icon list a QVector, and mixing the two index types in one
    // comparison is the sign-compare warning this used to produce.
    const std::size_t navCount = std::min(static_cast<std::size_t>(m_navIcons.size()),
                                          m_navButtons.size());
    for (std::size_t i = 0; i < navCount; ++i) {
        const NavButton& entry = m_navButtons[i];
        if (!entry.button) {
            continue;
        }
        const bool active = entry.pageIndex == current;
        entry.button->setIconSize(QSize(20, 20));
        entry.button->setIcon(appIcon(m_navIcons[static_cast<qsizetype>(i)],
                                      active ? accent : text,
                                      20));
    }

    // The top bar's four squares hold their icon in a QLabel rather than on the
    // button, because QPushButton cannot stack one over a text. So it is drawn
    // here for the same reason as above, and it follows the same state: the accent
    // on the active square, the muted colour on the rest, which is the colour the
    // stylesheet gives their labels, so icon and name do not disagree.
    const std::size_t quickCount = std::min(static_cast<std::size_t>(m_quickNavIcons.size()),
                                            m_quickNavButtons.size());
    for (std::size_t i = 0; i < quickCount; ++i) {
        const NavButton& entry = m_quickNavButtons[i];
        if (!entry.button) {
            continue;
        }
        auto* icon = entry.button->findChild<QLabel*>(QStringLiteral("quickNavIcon"));
        if (!icon) {
            continue;
        }
        const bool active = entry.pageIndex == current;
        icon->setPixmap(appIcon(m_quickNavIcons[static_cast<qsizetype>(i)],
                                active ? accent : muted,
                                24)
                            .pixmap(24, 24));
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