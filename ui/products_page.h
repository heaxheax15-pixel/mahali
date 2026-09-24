#pragma once

#include <QWidget>

#include "data/database.h"

class QLineEdit;
class QPushButton;
class QTableWidget;

namespace app::ui {

// Products (الكـتالوج): search by barcode/name, add/edit, and adjust stock.
// Prices are entered in decimal currency and stored as integer cents.
class ProductsPage : public QWidget {
    Q_OBJECT

public:
    explicit ProductsPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int rowCount() const;
    QTableWidget* table() const { return m_table; }
    QLineEdit* searchBox() const { return m_search; }

private slots:
    void onSearchChanged(const QString& text);
    void onAddClicked();
    void onEditClicked();
    void onStockClicked();
    void onSelectionChanged();

private:
    int selectedProductId() const;

    app::data::Database& m_db;
    QLineEdit* m_search;
    QPushButton* m_add;
    QPushButton* m_edit;
    QPushButton* m_stock;
    QTableWidget* m_table;
};

} // namespace app::ui