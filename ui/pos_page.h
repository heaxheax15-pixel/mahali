#pragma once

#include <optional>

#include <QString>
#include <QVector>
#include <QWidget>

#include "core/product.h"
#include "data/database.h"

class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;
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

private slots:
    void onRemoveLine();
    void onClearCart();
    void onCellChanged(int row, int column);
    void onCellDoubleClicked(int row, int column);
    void onQuickItemClicked(int productId);
    void onAddQuickProduct();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildTable();
    void refreshTotals();
    bool syncFromTable();
    // Looks up by barcode first, then by exact name, since the entry field
    // advertises both. Returns nothing when neither matches.
    std::optional<core::Product> findProduct(const QString& text) const;
    // Appends the product as a new line, or bumps the quantity of the line it
    // is already on. Assumes the caller has already cleared the entry field.
    void addProductToCart(const core::Product& product, long long quantity);

    void setNotice(const QString& text, bool ok);
    void refreshSessionChip();

    app::data::Database& m_db;
    QVector<PosLine> m_lines;
    QuickItemsBar* m_quickItems = nullptr;
    QLineEdit* m_entry;
    QPushButton* m_save;
    QTableWidget* m_table;
    QLabel* m_countLabel;
    QLabel* m_totalLabel;
    QLabel* m_notice;
    QLabel* m_sessionChip = nullptr;
    int m_lastSaleId = 0;
    bool m_updating = false;
};

} // namespace app::ui
