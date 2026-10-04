#pragma once

#include <optional>

#include <QString>
#include <QVector>
#include <QWidget>

#include "core/product.h"
#include "data/database.h"

class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTableWidget;

namespace app::ui {

class QuickItemsBar;

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
    long long adjustmentCents() const;
    long long lineQuantityAt(int row) const;
    long long linePriceAt(int row) const;
    int lastSaleId() const;
    QString noticeText() const;
    void setEntryText(const QString& text);
    void setAdjustmentText(const QString& text);
    QTableWidget* table() const { return m_table; }
    QLineEdit* entryField() const { return m_entry; }
    QLineEdit* adjustmentField() const { return m_adjustment; }
    QuickItemsBar* quickItemsBar() const { return m_quickItems; }

public slots:
    void addEntry();
    void completeSale();

    // Puts the register on somebody's account: asks who, names them and what they
    // already owe above the scan field, and points the sale button at their ledger.
    // The next completed sale is written as a debt instead of into the till.
    // clearCreditMode() puts it back, and isInCreditMode() says which of the two a
    // sale would go to.
    void enterCreditMode();
    void clearCreditMode();
    bool isInCreditMode() const { return m_creditCustomerId > 0; }

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
    void onAdjustmentChanged(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    // Reads the adjustment field as cents. nullopt when the field holds text that
    // is not money — the caller refuses the sale rather than quietly recording
    // no adjustment, because a cashier who typed something and got a sale
    // recorded without it would have no way of knowing.
    std::optional<long long> adjustmentFromField() const;

    // The sale grid, which is the wide table on the left. Named rather than
    // spelled as literals at each use: the quantity is read back by two
    // different code paths (the inline edit and the pre-sale sync) and a number
    // typed twice is a number that can drift apart.
    enum Column {
        NameColumn = 0,
        BarcodeColumn,
        UnitColumn,
        QuantityColumn,
        PriceColumn,
        ColumnCount,
    };

    void rebuildTable();
    void refreshTotals();
    bool syncFromTable();
    // The quick items are the only browsable list of products the page has left:
    // the catalogue table is gone, so a product that has no quick item can only
    // be brought in by its barcode.
    void refreshQuickItems();
    // Looks up by barcode first, then by exact name, since the entry field
    // advertises both. Returns nothing when neither matches.
    std::optional<core::Product> findProduct(const QString& text) const;
    // Appends the product as a new line, or bumps the quantity of the line it
    // is already on. Assumes the caller has already cleared the entry field.
    void addProductToCart(const core::Product& product, long long quantity);

    void setNotice(const QString& text, bool ok);

    app::data::Database& m_db;
    QVector<PosLine> m_lines;
    QSplitter* m_workspace = nullptr;
    QStackedWidget* m_cartStack = nullptr;
    QuickItemsBar* m_quickItems = nullptr;
    QLineEdit* m_entry;
    QPushButton* m_save;
    QTableWidget* m_table;
    // The narrow table on the right, deliberately kept in the layout with no
    // rows: it is the slot the grid used to be built in, and a cashier still
    // reads the sale as "two panes" before anything changes about the second.
    QTableWidget* m_emptyTable = nullptr;
    QLabel* m_countLabel;
    QLabel* m_totalLabel;
    QLabel* m_paidLabel;
    QLabel* m_remainingLabel;
    // The whole-invoice change, typed by the cashier: a surcharge or a discount.
    // A field of its own rather than another column of the grid, because it does
    // not belong to any line and has no product behind it.
    QLineEdit* m_adjustment = nullptr;
    // The adjustment as the cashier last left it, read back through
    // adjustmentFromField(). Cached so that repainting the grid does not have to
    // re-parse the field, and so a half-typed "-" does not throw the row away
    // every keystroke.
    long long m_adjustmentCents = 0;
    QLabel* m_notice;
    int m_lastSaleId = 0;
    // Who the sale in progress is being put on, and 0 for the till. Zero doubles
    // as "this is a cash sale", which is why isInCreditMode() is a comparison and
    // not a flag of its own that could disagree with it.
    int m_creditCustomerId = 0;
    // The bar that names the customer above the scan field, hidden while the sale
    // is going to the till.
    QFrame* m_creditBar = nullptr;
    QLabel* m_creditLabel = nullptr;
    bool m_updating = false;
};

} // namespace app::ui
