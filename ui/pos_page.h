#pragma once

#include <optional>

#include <QString>
#include <QVector>
#include <QWidget>

#include "core/product.h"
#include "data/database.h"

class QEvent;
class QLabel;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSortFilterProxyModel;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTableWidget;

namespace app::ui {

class QuickItemsBar;
class PosProductModel;

struct PosLine {
    int productId = 0;
    QString barcode;
    QString name;
    QString unit;
    long long quantity = 0;
    long long unitPriceCents = 0;
    long long basePriceCents = 0;
};

// Fast keyboard-driven register (نقطة بيع): scan a barcode and press Enter to
// add a line, click a quick item to add it in one go, edit a quantity in the
// grid, press Enter on the empty entry field to save the sale. Manual price
// overrides are always allowed and every override is written to the audit log.
// Desktop sales are recorded directly through SaleService (they never enter the
// device sync outbox, so they do not count as a "device").
class PosPage : public QWidget {
    Q_OBJECT

public:
    explicit PosPage(app::data::Database& db, QWidget* parent = nullptr);

    // Test accessors.
    int lineCount() const;
    long long totalCents() const;
    long long lineQuantityAt(int row) const;
    long long linePriceAt(int row) const;
    int lastSaleId() const;
    QString noticeText() const;
    void setEntryText(const QString& text);
    QTableWidget* table() const { return m_table; }
    QLineEdit* entryField() const { return m_entry; }
    QuickItemsBar* quickItemsBar() const { return m_quickItems; }

public slots:
    void addEntry();
    void completeSale();
    void refreshCatalog();

    // Puts the caret in the scan field and selects what is in it. Called by the
    // shell each time the register becomes the visible page: the window gives
    // focus to whichever page it lands on, and a cashier who switches away and
    // back would otherwise be typing a barcode into a table cell.
    void focusEntry();

private slots:
    void onBarcodeTextChanged(const QString& text);
    void onRemoveLine();
    void onClearCart();
    void onCellChanged(int row, int column);
    void onCellDoubleClicked(int row, int column);
    void onQuickItemClicked(int productId);
    void onAddQuickProduct();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildTable();
    void adjustQuantity(int row, long long delta);
    void refreshTotals();
    bool syncFromTable();
    // Looks up by barcode first, then by exact name, since the entry field
    // advertises both. Returns nothing when neither matches.
    std::optional<core::Product> findProduct(const QString& text) const;
    // Appends the product as a new line, or bumps the quantity of the line it
    // is already on. Assumes the caller has already cleared the entry field.
    void addProductToCart(const core::Product& product, long long quantity);

    void setNotice(const QString& text, bool ok);

    app::data::Database& m_db;
    QVector<PosLine> m_lines;
    PosProductModel* m_productModel = nullptr;
    QSortFilterProxyModel* m_productFilter = nullptr;
    QTableView* m_productTable = nullptr;
    QComboBox* m_unitFilter = nullptr;
    QSplitter* m_workspace = nullptr;
    QStackedWidget* m_cartStack = nullptr;
    QuickItemsBar* m_quickItems = nullptr;
    QLineEdit* m_entry;
    QLineEdit* m_catalogSearch = nullptr;
    QPushButton* m_save;
    QTableWidget* m_table;
    QLabel* m_countLabel;
    QLabel* m_totalLabel;
    QLabel* m_paidLabel;
    QLabel* m_remainingLabel;
    QLabel* m_notice;
    int m_lastSaleId = 0;
    bool m_updating = false;
};

} // namespace app::ui
