#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include "data/database.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QTimer;

namespace app::ui {

// Products (الكتالوج): one search box across name and barcode, a row of filter
// chips above the grid, and prices and quantity edited straight in the cells.
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
    void onSearchChanged();
    void onFilterChipClicked();
    void onAddClicked();
    void onItemChanged(QTableWidgetItem* item);
    void onNameActivated(int row, int column);

private:
    int productIdAt(int row) const;
    // Moves the active styling onto the chip whose key was pressed.
    void setFilterActive(const QString& key);
    // Rewrites one cell from the row the database holds, so a rejected edit
    // puts back what is actually stored rather than what was typed.
    void restoreCell(int row, int column);
    bool saveEditedPrice(int productId, int column, const QString& text);
    bool applyQuantityEdit(int productId, long long oldQuantity, const QString& text);

    app::data::Database& m_db;
    QLineEdit* m_search;
    QPushButton* m_add;
    QTableWidget* m_table;
    QLabel* m_footer;
    QTimer* m_searchDebounce;
    QHash<QString, QPushButton*> m_chips;
    QString m_filterKey = QStringLiteral("all");
    // Set while refresh() fills the grid: the cell edits made there must not
    // look like the user typing.
    bool m_updating = false;
};

} // namespace app::ui
