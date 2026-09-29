#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include "core/customer.h"
#include "core/sale_item.h"
#include "data/database.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace app::ui {

// Customers (العملاء): one search box across name and phone, a row of filter
// chips above the grid, and the running debt of every customer. Double-clicking
// a row opens that customer's card, where a credit sale, a repayment and the
// merged history live.
class CustomersPage : public QWidget {
    Q_OBJECT

public:
    explicit CustomersPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int rowCount() const;
    QString balanceAt(int row) const;
    QString noticeText() const;
    int selectedCustomerId() const;
    QTableWidget* table() const { return m_table; }
    QLineEdit* searchBox() const { return m_search; }

    // The two writes the page still owns, kept public so the customer card and
    // anything else recording a debt can route through the same two calls
    // instead of opening its own transaction.
    void recordDebt(int customerId, const QVector<core::SaleItem>& items);
    void recordPayment(int customerId, long long amountCents, const QString& note);

private slots:
    void onSearchChanged();
    void onFilterChipClicked();
    void onAddClicked();
    void onRowActivated(int row, int column);

private:
    int customerIdAt(int row) const;
    // Moves the active styling onto the chip whose key was pressed.
    void setFilterActive(const QString& key);
    QString lastOperationAt(int customerId) const;

    app::data::Database& m_db;
    QLineEdit* m_search;
    QTableWidget* m_table;
    QLabel* m_footer;
    QLabel* m_notice;
    QTimer* m_searchDebounce;
    QHash<QString, QPushButton*> m_chips;
    QString m_filterKey = QStringLiteral("all");
    // Set while refresh() fills the grid: the changes made there must not look
    // like the user doing something.
    bool m_updating = false;
};

} // namespace app::ui
