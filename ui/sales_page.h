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

    // Test accessors. These read cached values captured in refresh(), so they stay
    // correct no matter which language the summary label is rendered in.
    void refresh();
    int rowCount() const;
    qint64 grandTotalCents() const;
    qint64 profitCents() const;
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
    qint64 m_cachedGrandTotalCents = 0;
    qint64 m_cachedProfitCents = 0;
};

} // namespace app::ui