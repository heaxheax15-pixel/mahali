#pragma once

#include <QWidget>

#include "data/database.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace app::ui {

// Expenses & owner drawings (المصاريف والسحوبات): money leaving the till on
// the central PC. Each entry is written to its ledger and to the open session
// as a negative cash movement (so the expected-till stays accurate).
class ExpensesPage : public QWidget {
    Q_OBJECT

public:
    explicit ExpensesPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int entryCount() const;
    long long expensesTotalCents() const;
    QTableWidget* table() const { return m_table; }
    QString noticeText() const;

public slots:
    void recordExpense(const QString& label, long long amountCents);
    void recordDrawing(const QString& note, long long amountCents);
    void reverseRow(int row);

private slots:
    void onExpenseClicked();
    void onDrawingClicked();
    void onReverseClicked();

private:
    app::data::Database& m_db;
    QTableWidget* m_table;
    QLabel* m_summary;
    QLabel* m_notice;
    QPushButton* m_expenseButton;
    QPushButton* m_drawingButton;
    QPushButton* m_reverseButton;
};

} // namespace app::ui