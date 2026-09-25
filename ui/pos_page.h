#pragma once

#include <QVector>
#include <QWidget>

#include "data/database.h"

class QEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace app::ui {

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
// add a line, edit quantity/price directly in the grid, press Enter on the
// empty entry field to save the sale. Manual price overrides are always
// allowed and every override is written to the audit log. Desktop sales are
// recorded directly through SaleService (they never enter the device sync
// outbox, so they do not count as a "device").
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

public slots:
    void addEntry();
    void completeSale();

private slots:
    void onRemoveLine();
    void onCellChanged(int row, int column);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildTable();
    void refreshTotals();
    bool syncFromTable();
    PosLine& lineForRow(int row);

    void setNotice(const QString& text, bool ok);
    void refreshSessionChip();

    app::data::Database& m_db;
    QVector<PosLine> m_lines;
    QLineEdit* m_entry;
    QPushButton* m_save;
    QTableWidget* m_table;
    QLabel* m_itemsLabel;
    QLabel* m_totalLabel;
    QLabel* m_notice;
    QLabel* m_sessionChip = nullptr;
    int m_lastSaleId = 0;
    bool m_updating = false;
};

} // namespace app::ui