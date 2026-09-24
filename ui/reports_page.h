#pragma once

#include <QWidget>

#include "data/database.h"
#include "data/report_service.h"

class QDateEdit;
class QLabel;
class QPushButton;
class QTableWidget;

namespace app::ui {

// Reports (التقارير): a profit/loss statement, the zakat base (2.5%) and the
// cash breakdown for any date range, with one-click presets for today,
// yesterday, this week, this month and everything.
class ReportsPage : public QWidget {
    Q_OBJECT

public:
    explicit ReportsPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    const data::StoreReport& report() const { return m_report; }
    QTableWidget* cashTable() const { return m_cashTable; }

private slots:
    void onToday();
    void onYesterday();
    void onThisWeek();
    void onThisMonth();
    void onAll();
    void onRangeChanged();

private:
    void fromTo(const QDateTime& from, const QDateTime& to);
    void rebuild();

    app::data::Database& m_db;
    QDateEdit* m_fromEdit = nullptr;
    QDateEdit* m_toEdit = nullptr;
    QLabel* m_summary;
    QLabel* m_costs;
    QLabel* m_bottom;
    QTableWidget* m_cashTable;
    data::StoreReport m_report;
};

} // namespace app::ui