#pragma once

#include <QWidget>

#include "data/database.h"

class QPushButton;
class QTableWidget;

namespace app::ui {

// Suppliers (الموردون): catalog of suppliers plus their credit invoices
// (فاتورة آجلة = SupplierTransaction). Selecting a supplier shows its history.
class SuppliersPage : public QWidget {
    Q_OBJECT

public:
    explicit SuppliersPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int supplierCount() const;
    int transactionCount() const;
    QTableWidget* suppliersTable() const { return m_suppliers; }
    QTableWidget* transactionsTable() const { return m_transactions; }

private slots:
    void onAddClicked();
    void onEditClicked();
    void onAddTransactionClicked();
    void onSelectionChanged();

private:
    int selectedSupplierId() const;
    void reloadTransactions();

    app::data::Database& m_db;
    QPushButton* m_addInvoice = nullptr;
    QTableWidget* m_suppliers;
    QTableWidget* m_transactions;
};

} // namespace app::ui