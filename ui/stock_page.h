#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

#include "data/database.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace app::ui {

// Stock (المخزون): the same catalogue as the products page, read as quantities
// rather than as prices. Nothing here edits anything -- a stock page that could
// change a count would be a second place to change it, and the one that sells is
// the till. What it does is make the three counts worth acting on findable: the
// low band, the negative ones and the sold-out ones, each behind a chip.
//
// Rows whose name holds no letter are left out, on the same rule the products
// list uses. That is why a negative count whose name is "12345" does not appear
// here either: an import left that row with no name to show, and the products page
// already treats it as not yet a product. Fixing the name is the way to bring the
// row into this page.
class StockPage : public QWidget {
    Q_OBJECT

public:
    explicit StockPage(app::data::Database& db, QWidget* parent = nullptr);

    void refresh();
    int rowCount() const;
    QTableWidget* table() const { return m_table; }
    QLineEdit* searchBox() const { return m_search; }

private slots:
    void onSearchChanged();
    void onFilterChipClicked();

private:
    // Moves the active styling onto the chip whose key was pressed.
    void setFilterActive(const QString& key);

    app::data::Database& m_db;
    QLineEdit* m_search;
    QTableWidget* m_table;
    QLabel* m_footer;
    QTimer* m_searchDebounce;
    QHash<QString, QPushButton*> m_chips;
    QString m_filterKey = QStringLiteral("all");
};

} // namespace app::ui
