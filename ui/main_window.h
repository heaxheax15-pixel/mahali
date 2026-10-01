#pragma once

#include <QMainWindow>

#include "data/database.h"
#include "server_controller.h"
#include "widgets/app_icon.h"

#include <QVector>

#include <functional>
#include <vector>

class QLabel;
class QFrame;
class QPropertyAnimation;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

namespace app::core {
class UpdateChecker;
class UpdateDownloader;
}

namespace app::ui {

class AuditLogPage;
class CashSessionPage;
class CustomersPage;
class ExpensesPage;
class PosPage;
class ProductsPage;
class RefundsPage;
class ReportsPage;
class SalesPage;
class SettingsPage;
class SuppliersPage;
class UsersPage;

// One line of the sidebar: the icon to draw, the label to show, and the page it
// opens. Collected per group when the sidebar is built.
struct NavEntry {
    Icon icon;
    QString label;
    int pageIndex;
};

// A sidebar button paired with the page it opens, kept so the active one can be
// repainted from the page the stacked widget is actually showing.
struct NavButton {
    QPushButton* button = nullptr;
    int pageIndex = 0;
};

// One page of the shell, held as how to make it rather than as the page itself.
// Every page constructor reads the database and fills a table, so building all
// fourteen up front cost half a second and nearly six thousand rows of table for
// a window that shows one page at a time. The factory table is in the same order
// as the page indices the sidebar uses; an entry is filled in the first time its
// page is opened and the instance kept from then on, so opening a page twice
// re-reads the same widget rather than building a second one.
struct PageFactory {
    std::function<QWidget*()> create;
    // Asks the built page to re-read. Null where the original page had no
    // refresh on entry, so a page is not given behaviour it never had.
    std::function<void(QWidget*)> refresh;
    QWidget* instance = nullptr;
};

// The desktop shell: an Arabic RTL window with a top bar spanning the full
// width, a grouped sidebar of pages on the right, the active page on the left,
// and a live sync status line in the status bar.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(app::data::Database& db, ServerController& controller,
                        QWidget* parent = nullptr);

    // Each builds its page on first ask, so a caller gets a live page whichever
    // order the window and the caller reach it in. The window opens on the quick
    // sale, so posPage() is already built by the time anything else runs.
    PosPage* posPage();
    CashSessionPage* cashSessionPage();
    SalesPage* salesPage();
    ExpensesPage* expensesPage();
    ReportsPage* reportsPage();
    SettingsPage* settingsPage();
    RefundsPage* refundsPage();
    AuditLogPage* auditLogPage();
    UsersPage* usersPage();

    // Switching the running occasion, for the page that will own the control.
    // Each call re-reads the bar, so the label cannot drift from the setting.
    bool activateOccasion(int occasionId);
    void deactivateOccasion();
    QString occasionLabelText() const;

private slots:
    void onPageChanged(int row);
    void onSwitchUserClicked();
    void rebuildNav();
    // Flips the sidebar between shown and hidden and remembers the choice, so the
    // window opens the way it was left. The tables take the rail's width back on
    // their own: the rail is one widget in the body's row layout, and a hidden
    // widget is skipped by the layout, leaving the rest to the page's stretch.
    void onSidebarToggleClicked();
    // Flips the theme between light and dark, stores the choice, and re-paints
    // the toggle so it always offers the theme it would switch to.
    void onThemeToggleClicked();

    // Update signals. Only updateAvailable touches the UI: a check that finds
    // nothing new, or that cannot reach GitHub, is logged and otherwise
    // ignored — an update notice must never interrupt the shop's work.
    void onUpdateAvailable(const QString& tag, const QString& notes);
    void onUpdateUpToDate();
    void onUpdateFailed(const QString& reason);

    // The download behind the bar's "تنزيل" button, and the batch script that
    // unpacks it once the app has quit.
    void onUpdateDownloadClicked();
    void onUpdateDownloadProgress(int percent);
    void onUpdateDownloadFinished(const QString& path);
    void onUpdateDownloadFailed(const QString& reason);
    void onRestartToInstall();

private:
    void refreshOccasionLabel();
    void buildNavForRole(const QString& role);
    void showUpdateBar(const QString& tag);
    QWidget* buildTopBar();
    QWidget* buildSidebar();
    // Repaints the top bar's icons for the theme in use. The icons are pixmaps,
    // so the stylesheet cannot recolour them: they are drawn here, and every
    // change of theme comes back through this.
    void refreshThemeIcons();
    void addNavGroup(const QString& title, const std::vector<NavEntry>& entries);
    // Repaints every nav button from the page on screen, so the highlighted one
    // always agrees with the stacked widget even after a programmatic change.
    void refreshNavActiveState();
    // Builds the page at this index if it is still a placeholder, and returns it.
    // The stacked widget holds a cheap empty widget per index until then, so
    // this swaps the real page into the same slot and the index every other part
    // of the window uses keeps pointing at the same page.
    QWidget* ensurePage(int row);
    // Asks the page at this index to re-read, doing nothing if it has not been
    // opened yet: a page nobody has looked at has nothing stale to correct.
    void refreshPage(int row);

    app::data::Database& m_db;
    ServerController& m_controller;
    // The sidebar's buttons, in the order they were added. The stacked widget
    // is the source of truth for which page is showing; this only holds the
    // widgets so the active one can be repainted.
    std::vector<NavButton> m_navButtons;
    // The icon each nav button was built with, in the same order as
    // m_navButtons. The buttons are rebuilt from scratch when the role changes,
    // so this is cleared and refilled alongside them; keeping the icon beside
    // the button is what lets the sidebar be recoloured without rebuilding it.
    QVector<Icon> m_navIcons;
    // The group's layout buttons are added to. Kept so a role change can clear
    // the sidebar without rebuilding the whole window.
    QWidget* m_sidebar = nullptr;
    // Whether the rail is showing, kept alongside the widget because the choice is
    // written to the settings and has to be re-applied on the next start, long
    // after the rail itself has been built.
    bool m_sidebarVisible = true;
    // The column the nav groups are added to. Held so a role change can empty
    // it and build the groups again without rebuilding the window.
    QVBoxLayout* m_sidebarGroupLayout = nullptr;
    QStackedWidget* m_pages;
    // The four square buttons in the top bar, in the order they were built. Kept
    // apart from m_navButtons because those are thrown away and rebuilt whenever
    // the signed-in role changes, while these four are built once with the bar and
    // must survive that: they are not role-dependent, so a role change has no
    // reason to unhook the page switching.
    std::vector<NavButton> m_quickNavButtons;
    // The strip above the sidebar and the pages. Kept so the icons on it can be
    // repainted when the theme changes, rather than only at construction.
    QWidget* m_topBar = nullptr;
    QPushButton* m_themeToggle = nullptr;
    QPushButton* m_settingsBtn = nullptr;
    // The ☰ in the top bar. It lives in the window and not in the rail, because the
    // rail is the thing it hides: a control inside the thing it controls cannot
    // bring it back.
    QPushButton* m_sidebarToggle = nullptr;
    // Created lazily, on the first newer release, so the app opens with no bar
    // at all rather than an empty one.
    QFrame* m_updateBar = nullptr;
    app::core::UpdateChecker* m_updateChecker;
    app::core::UpdateDownloader* m_updateDownloader;
    // Both live inside the bar; kept so the download handlers can drive the
    // button they belong to without hunting for it.
    QPushButton* m_updateDownloadBtn = nullptr;
    QPushButton* m_updateRestartBtn = nullptr;
    // The signed-in user, as one string: the prefix and the name together, so
    // the layout cannot put the name on the wrong side of the colon.
    QLabel* m_userLabel = nullptr;
    // Shows the occasion running right now, and stays empty when there is none.
    // Always present rather than created on demand, so activation and
    // deactivation only have to change its text.
    QLabel* m_occasionLabel = nullptr;
    // The app's mark. A pixmap, which a stylesheet cannot recolour, so it is
    // redrawn by refreshThemeIcons along with the buttons rather than styled.
    QLabel* m_brandIcon = nullptr;
    QPropertyAnimation* m_fade = nullptr;
    // The pages, in page-index order, held as factories until each is opened.
    // The typed pointers this replaced are not needed: the entry knows its own
    // type through create() and refresh(), and the accessors cast on the way out.
    QVector<PageFactory> m_pageFactories;
};

} // namespace app::ui