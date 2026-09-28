#pragma once

#include <QMainWindow>

#include "data/database.h"
#include "server_controller.h"

class QLabel;
class QListWidget;
class QFrame;
class QPropertyAnimation;
class QPushButton;
class QStackedWidget;

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

// The desktop shell: Arabic RTL layout, a sidebar of pages on the right, the
// active page on the left, and a live sync status line in the status bar.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(app::data::Database& db, ServerController& controller,
                        QWidget* parent = nullptr);

    PosPage* posPage() const { return m_pos; }
    CashSessionPage* cashSessionPage() const { return m_cashSession; }
    SalesPage* salesPage() const { return m_sales; }
    ExpensesPage* expensesPage() const { return m_expenses; }
    ReportsPage* reportsPage() const { return m_reports; }
    SettingsPage* settingsPage() const { return m_settings; }
    RefundsPage* refundsPage() const { return m_refunds; }
    AuditLogPage* auditLogPage() const { return m_auditLog; }
    UsersPage* usersPage() const { return m_usersPage; }

    // Switching the running occasion, for the page that will own the control.
    // Each call re-reads the bar, so the label cannot drift from the setting.
    bool activateOccasion(int occasionId);
    void deactivateOccasion();
    QString occasionLabelText() const;

private slots:
    void onSyncStatusChanged();
    void onPageChanged(int row);
    void onSwitchUserClicked();
    void rebuildNav();

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

    app::data::Database& m_db;
    ServerController& m_controller;
    QListWidget* m_nav;
    QStackedWidget* m_pages;
    // Created lazily, on the first newer release, so the app opens with no bar
    // at all rather than an empty one.
    QFrame* m_updateBar = nullptr;
    app::core::UpdateChecker* m_updateChecker;
    app::core::UpdateDownloader* m_updateDownloader;
    // Both live inside the bar; kept so the download handlers can drive the
    // button they belong to without hunting for it.
    QPushButton* m_updateDownloadBtn = nullptr;
    QPushButton* m_updateRestartBtn = nullptr;
    QLabel* m_statusLabel;
    QLabel* m_userLabel = nullptr;
    // Shows the occasion running right now, and stays empty when there is none.
    // Always present rather than created on demand, so activation and
    // deactivation only have to change its text.
    QLabel* m_occasionLabel = nullptr;
    QPropertyAnimation* m_fade = nullptr;
    PosPage* m_pos = nullptr;
    CashSessionPage* m_cashSession = nullptr;
    ProductsPage* m_products = nullptr;
    CustomersPage* m_customers = nullptr;
    SuppliersPage* m_suppliers = nullptr;
    SalesPage* m_sales = nullptr;
    ExpensesPage* m_expenses = nullptr;
    ReportsPage* m_reports = nullptr;
    SettingsPage* m_settings = nullptr;
    RefundsPage* m_refunds = nullptr;
    AuditLogPage* m_auditLog = nullptr;
    UsersPage* m_usersPage = nullptr;
};

} // namespace app::ui