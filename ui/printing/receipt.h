#pragma once

#include <QDateTime>
#include <QString>
#include <vector>

class QTextDocument;

namespace app::data { class Database; }

namespace app::ui {

enum class ReceiptPaper {
    Thermal58,
    Thermal80,
    A5,
    A4,
};

struct ReceiptLine {
    QString productName;
    QString unitLabel;       // already translated: قطعة/كرتونة or Pièce/Carton
    long long quantity = 0;
    long long unitPriceCents = 0;
    long long lineTotalCents = 0;
};

struct ReceiptData {
    QString receiptNumber;   // "A-2026-0001"; may be empty for pre-15 rows
    QDateTime dateTime;
    QString shopName;
    QString currencySymbol;
    QString customerName;    // empty for a walk-in sale
    bool isCredit = false;
    std::vector<ReceiptLine> lines;
    long long totalCents = 0;
    long long adjustmentCents = 0;
    // The line under the totals block. Read from the setting
    // receipt_footer_text by the loaders; empty means the renderer falls
    // back to the translated default.
    QString footerText;
};

// Loads everything a receipt needs for a cash sale (sales row).
ReceiptData buildReceiptForSale(app::data::Database& db, int saleId);

// Same for a credit transaction (customer_transactions row).
ReceiptData buildReceiptForCustomerTransaction(app::data::Database& db,
                                               int transactionId);

// Renders the receipt to a QTextDocument. The caller owns the pointer.
// Paper size changes the width and, for Thermal58, drops the unit column:
// five columns do not fit on a 58mm roll and the cashier already knows
// what they scanned.
QTextDocument* renderReceipt(const ReceiptData& data, ReceiptPaper paper);

// Reads settings.receipt_paper_size (values "58", "80", "a5", "a4") and
// returns the enum. Falls back to Thermal80 when the key is absent or
// holds anything else, because that is the shape the printer the shop
// already owns emits.
ReceiptPaper currentReceiptPaper(app::data::Database& db);

// The inverse: turns the enum into the string the settings page stores.
QString paperSettingValue(ReceiptPaper paper);

} // namespace app::ui
