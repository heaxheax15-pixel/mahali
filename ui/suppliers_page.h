#pragma once

#include <QWidget>

#include "data/database.h"

class QTableWidget;

namespace app::ui {

// Suppliers (الموردون): catalog of suppliers. The balance a supplier is owed
// is not listed here: it is read from the invoices and payments, which the
// purchases screen owns.
class SuppliersPage : public QWidget {
    Q_OBJECT

public:
    explicit SuppliersPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int supplierCount() const;
    QTableWidget* suppliersTable() const { return m_suppliers; }

private slots:
    void onAddClicked();
    void onEditClicked();

private:
    int selectedSupplierId() const;

    app::data::Database& m_db;
    QTableWidget* m_suppliers;
};

} // namespace app::ui