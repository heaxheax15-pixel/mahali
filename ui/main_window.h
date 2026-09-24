#pragma once

#include <QMainWindow>

#include "data/database.h"
#include "server_controller.h"

class QLabel;
class QListWidget;
class QStackedWidget;

namespace app::ui {

class AuditLogPage;
class CashSessionPage;
class ExpensesPage;
class PosPage;
class RefundsPage;
class ReportsPage;
class SalesPage;
class SettingsPage;

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

private slots:
    void onSyncStatusChanged();

private:
    app::data::Database& m_db;
    ServerController& m_controller;
    QListWidget* m_nav;
    QStackedWidget* m_pages;
    QLabel* m_statusLabel;
    PosPage* m_pos = nullptr;
    CashSessionPage* m_cashSession = nullptr;
    SalesPage* m_sales = nullptr;
    ExpensesPage* m_expenses = nullptr;
    ReportsPage* m_reports = nullptr;
    SettingsPage* m_settings = nullptr;
    RefundsPage* m_refunds = nullptr;
    AuditLogPage* m_auditLog = nullptr;
};

} // namespace app::ui