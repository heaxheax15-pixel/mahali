#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include "core/supplier.h"
#include "data/database.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace app::ui {

// Suppliers (الموردون): one search box across name and phone, a row of filter
// chips above the grid, and what every supplier is owed. Double-clicking a row
// opens that supplier's card, where the invoices, the payments, the returns and
// the merged history live.
class SuppliersPage : public QWidget {
    Q_OBJECT

public:
    explicit SuppliersPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();

    int rowCount() const;
    // Kept for the UI test and for anything that grew up with the old page.
    int supplierCount() const;
    QString balanceAt(int row) const;
    QString footerText() const;
    int supplierIdAt(int row) const;
    int selectedSupplierId() const;
    QTableWidget* table() const { return m_table; }
    QLineEdit* searchBox() const { return m_search; }

private slots:
    void onSearchChanged();
    void onFilterChipClicked();
    void onAddClicked();
    void onRowActivated(int row, int column);

private:
    // Moves the active styling onto the chip whose key was pressed.
    void setFilterActive(const QString& key);
    // Invoices for this supplier that still carry a balance, paid lines excluded.
    int unpaidInvoiceCount(int supplierId) const;

    app::data::Database& m_db;
    QLineEdit* m_search;
    QTableWidget* m_table;
    QLabel* m_footer;
    QTimer* m_searchDebounce;
    QHash<QString, QPushButton*> m_chips;
    QString m_filterKey = QStringLiteral("all");
    // Set while refresh() fills the grid: the changes made there must not look
    // like the user doing something.
    bool m_updating = false;
};

} // namespace app::ui
