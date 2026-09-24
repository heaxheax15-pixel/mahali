#pragma once

#include <QWidget>

#include "data/database.h"

class QTableWidget;

namespace app::ui {

// Customers (الحسابات): the debt ledger. Balance owed = what they bought on
// credit minus what they paid (reversals are already negative entries).
class CustomersPage : public QWidget {
    Q_OBJECT

public:
    explicit CustomersPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int rowCount() const;
    QTableWidget* table() const { return m_table; }
    QString balanceAt(int row) const;

private slots:
    void onAddClicked();
    void onEditClicked();
    void onSelectionChanged();

private:
    int selectedCustomerId() const;

    app::data::Database& m_db;
    QTableWidget* m_table;
};

} // namespace app::ui