#pragma once

#include <QWidget>

#include "data/database.h"

class QLabel;
class QTableWidget;

namespace app::ui {

class StatCard;

// Today's sales (مبيعات اليوم): every sale recorded from the PC or synced from
// any device, with a live summary (count, net total, approximate profit) and a
// line-item detail view per sale.
class SalesPage : public QWidget {
    Q_OBJECT

public:
    explicit SalesPage(app::data::Database& db, QWidget* parent = nullptr);

    // Test accessors.
    void refresh();
    int rowCount() const;
    long long grandTotalCents() const;
    long long profitCents() const;
    QTableWidget* table() const { return m_table; }

private slots:
    void showDetails();

private:
    app::data::Database& m_db;
    QTableWidget* m_table;
    QLabel* m_summary;
    StatCard* m_countCard = nullptr;
    StatCard* m_totalCard = nullptr;
    StatCard* m_profitCard = nullptr;
};

} // namespace app::ui