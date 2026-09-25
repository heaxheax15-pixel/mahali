#pragma once

#include <QWidget>

#include "data/database.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace app::ui {

class PageHeader;
class StatCard;

// Cash session (جلسة الصندوق): one open shared daily session per day on the
// central PC. Shows the current session, its opening float and movement sum
// (expected in the till), lets the operator open and close it, and reports the
// variance between the counted till and expectation after closing.
class CashSessionPage : public QWidget {
    Q_OBJECT

public:
    explicit CashSessionPage(app::data::Database& db, QWidget* parent = nullptr);

    // Test accessors.
    void refresh();
    bool hasOpenSession() const;
    int sessionId() const;
    int movementCount() const;
    long long expectedCents() const;
    long long lastVarianceCents() const;
    QTableWidget* table() const { return m_table; }
    QPushButton* openButton() const { return m_openButton; }

public slots:
    void openSession(long long openingFloatCents);
    void closeSession(long long closingCountedCents);

private slots:
    void onOpenClicked();
    void onCloseClicked();

private:
    app::data::Database& m_db;
    QTableWidget* m_table;
    QLabel* m_summary;
    QLabel* m_variance;
    QPushButton* m_openButton;
    QPushButton* m_closeButton;
    PageHeader* m_header = nullptr;
    StatCard* m_floatCard = nullptr;
    StatCard* m_movementsCard = nullptr;
    StatCard* m_expectedCard = nullptr;
    StatCard* m_varianceCard = nullptr;
    int m_sessionId = 0;
    long long m_expectedCents = 0;
    long long m_lastVarianceCents = 0;
    bool m_hasOpen = false;
};

} // namespace app::ui