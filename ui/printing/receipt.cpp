#include "receipt.h"

#include <QCoreApplication>
#include <QFont>
#include <QLocale>
#include <QSqlQuery>
#include <QTextDocument>
#include <QVariant>

#include <optional>

#include "core/format_utils.h"
#include "data/customer_repository.h"
#include "data/customer_transaction_item_repository.h"
#include "data/customer_transaction_repository.h"
#include "data/product_repository.h"
#include "data/sale_item_repository.h"
#include "data/sale_repository.h"
#include "data/sale_rules.h"
#include "data/setting_repository.h"

namespace app::ui {

namespace {

QString translatedPackageName(const QString& stored)
{
    // Same call pos_page.cpp makes when it labels the Unité column: the context
    // is repeated rather than shared because displayPackageName() is file-local
    // there, and the word has to look identical in both places.
    if (stored.isEmpty() || stored == QStringLiteral("كرتونة")) {
        return QCoreApplication::translate("app::ui::PosPage", "كرتونة");
    }
    return stored;
}

QString unitLabelFor(const QString& unitKind, const QString& packageName)
{
    if (unitKind == QStringLiteral("weight")) {
        return QCoreApplication::translate("app::ui::Receipt", "غرام");
    }
    if (unitKind == QStringLiteral("package")) {
        return translatedPackageName(packageName);
    }
    return QCoreApplication::translate("app::ui::PosPage", "قطعة");
}

// unitPriceCents * quantity, or 0 when the product would not fit in a long
// long. A receipt is not a place to refuse: an impossible line is dropped to a
// zero total and the rest of the paper still prints.
long long lineTotalCentsFor(long long unitPriceCents, long long quantity,
                                   const QString& unitKind)
{
    if (!data::detail::productFits(unitPriceCents, quantity)) {
        return 0;
    }
    long long term = unitPriceCents * quantity;
    if (unitKind == QStringLiteral("weight")) {
        term /= 1000;
    }
    return term;
}

QString resolvedProductName(const std::optional<core::Product>& product, int productId)
{
    return product ? product->name : QStringLiteral("(#%1)").arg(productId);
}

} // namespace

ReceiptData buildReceiptForSale(app::data::Database& db, int saleId)
{
    data::SaleRepository sales(db);
    const auto sale = sales.findById(saleId);
    if (!sale) {
        return {};
    }

    ReceiptData receipt;
    receipt.receiptNumber = sale->receiptNumber;
    receipt.dateTime = sale->createdAt;
    receipt.adjustmentCents = sale->adjustmentCents;

    data::SettingRepository settings(db);
    receipt.shopName = settings.value(QStringLiteral("shop_name")).value_or(QString());
    receipt.currencySymbol = settings.value(QStringLiteral("currency_symbol")).value_or(QString());
    receipt.footerText = settings.value(QStringLiteral("receipt_footer_text")).value_or(QString());

    // core::Sale has no customerId member yet — the column exists but nothing
    // writes it. Reading it here means the receipt is already correct the day
    // the till starts attaching a customer to a cash sale, with no change to
    // the loader.
    int customerId = 0;
    {
        QSqlQuery query(db.handle());
        query.prepare(QStringLiteral("SELECT customer_id FROM sales WHERE id = ?"));
        query.addBindValue(saleId);
        if (query.exec() && query.next() && !query.value(0).isNull()) {
            customerId = query.value(0).toInt();
        }
    }
    if (customerId > 0) {
        data::CustomerRepository customers(db);
        if (const auto customer = customers.findById(customerId)) {
            receipt.customerName = customer->name;
        }
    }

    data::ProductRepository products(db);
    data::SaleItemRepository items(db);
    long long linesTotal = 0;
    for (const core::SaleItem& item : items.findBySaleId(saleId)) {
        const auto product = products.findById(item.productId);
        ReceiptLine line;
        line.productName = resolvedProductName(product, item.productId);
        line.unitLabel = unitLabelFor(item.unitKind, product ? product->packageName : QString());
        line.quantity = item.quantity;
        line.unitPriceCents = item.unitPriceCents;
        line.lineTotalCents = lineTotalCentsFor(item.unitPriceCents, item.quantity, item.unitKind);
        linesTotal += line.lineTotalCents;
        receipt.lines.push_back(line);
    }
    // totalCents is the sum of the lines, not the sale row's total_cents: the
    // row already folds in adjustment_cents (its invariant is
    // total = Σlines + adjustment), and the paper shows the two apart before
    // the payable. Setting it to the row total and then adding the adjustment
    // again would count it twice.
    receipt.totalCents = linesTotal;

    return receipt;
}

ReceiptData buildReceiptForCustomerTransaction(app::data::Database& db, int transactionId)
{
    data::CustomerTransactionRepository transactions(db);
    const auto transaction = transactions.findById(transactionId);
    if (!transaction) {
        return {};
    }

    ReceiptData receipt;
    receipt.receiptNumber = transaction->receiptNumber;
    receipt.dateTime = transaction->createdAt;
    receipt.adjustmentCents = transaction->adjustmentCents;
    receipt.isCredit = true;

    data::SettingRepository settings(db);
    receipt.shopName = settings.value(QStringLiteral("shop_name")).value_or(QString());
    receipt.currencySymbol = settings.value(QStringLiteral("currency_symbol")).value_or(QString());
    receipt.footerText = settings.value(QStringLiteral("receipt_footer_text")).value_or(QString());

    data::CustomerRepository customers(db);
    if (const auto customer = customers.findById(transaction->customerId)) {
        receipt.customerName = customer->name;
    }

    data::ProductRepository products(db);
    data::CustomerTransactionItemRepository items(db);
    long long linesTotal = 0;
    for (const core::CustomerTransactionItem& item : items.findByTransactionId(transactionId)) {
        const auto product = products.findById(item.productId);
        ReceiptLine line;
        line.productName = resolvedProductName(product, item.productId);
        line.unitLabel = unitLabelFor(item.unitKind, product ? product->packageName : QString());
        line.quantity = item.quantity;
        line.unitPriceCents = item.unitPriceCents;
        line.lineTotalCents = lineTotalCentsFor(item.unitPriceCents, item.quantity, item.unitKind);
        linesTotal += line.lineTotalCents;
        receipt.lines.push_back(line);
    }
    receipt.totalCents = linesTotal;

    return receipt;
}

QTextDocument* renderReceipt(const ReceiptData& data, ReceiptPaper paper)
{
    const bool narrow = (paper == ReceiptPaper::Thermal58);

    int fontPointSize = 8;
    int textWidth = 280;
    switch (paper) {
    case ReceiptPaper::Thermal58:
        fontPointSize = 7;
        textWidth = 200;
        break;
    case ReceiptPaper::Thermal80:
        fontPointSize = 8;
        textWidth = 280;
        break;
    case ReceiptPaper::A5:
        fontPointSize = 10;
        textWidth = 480;
        break;
    case ReceiptPaper::A4:
        fontPointSize = 11;
        textWidth = 700;
        break;
    }

    // These widths are pixel approximations for the offscreen render, not
    // printer-accurate millimetres: the print phase will set a real page size.
    QString html = QStringLiteral("<html><body>");
    if (!data.shopName.isEmpty()) {
        html += QStringLiteral("<p style=\"text-align:center;font-weight:bold;\">%1</p>")
                    .arg(data.shopName.toHtmlEscaped());
    }
    html += QStringLiteral("<hr/>");
    html += QStringLiteral("<p>%1</p>")
                .arg(QCoreApplication::translate("app::ui::Receipt", "Reçu N° : %1")
                         .arg(data.receiptNumber.toHtmlEscaped()));
    html += QStringLiteral("<p>%1</p>")
                .arg(QLocale().toString(data.dateTime, QLocale::ShortFormat).toHtmlEscaped());
    if (!data.customerName.isEmpty()) {
        html += QStringLiteral("<p>%1</p>")
                    .arg(QCoreApplication::translate("app::ui::Receipt", "Client : %1")
                             .arg(data.customerName.toHtmlEscaped()));
    }
    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\"></table>");
    for (const ReceiptLine& line : data.lines) {
        html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"margin:0 0 6px 0;\">");
        html += QStringLiteral("<tr><td colspan=\"2\"><b>%1</b></td></tr>")
                    .arg(line.productName.toHtmlEscaped());
        QString detail;
        // A one-piece line reads as its price: "2388.00 DA", not
        // "1 Pièce × 2388.00 DA", which is the same figure with noise around it.
        const bool unitIsPiece = (line.unitLabel == QCoreApplication::translate("app::ui::PosPage", "قطعة"));
        if (line.quantity == 1 && unitIsPiece) {
            detail = formatMoney(line.unitPriceCents, data.currencySymbol);
        } else {
            detail = QStringLiteral("%1 %2 × %3")
                         .arg(line.quantity)
                         .arg(line.unitLabel.toHtmlEscaped())
                         .arg(formatMoney(line.unitPriceCents, data.currencySymbol));
        }
        html += QStringLiteral("<tr><td>%1</td><td align=\"right\" width=\"40%\">%2</td></tr>")
                    .arg(detail)
                    .arg(formatMoney(line.lineTotalCents, data.currencySymbol));
        html += QStringLiteral("</table>");
    }

    html += QStringLiteral("<hr/>");

    html += QStringLiteral("<p style=\"text-align:right;\">%1</p>")
                .arg(QCoreApplication::translate("app::ui::Receipt", "Total : %1")
                         .arg(formatMoney(data.totalCents, data.currencySymbol)));
    if (data.adjustmentCents != 0) {
        html += QStringLiteral("<p style=\"text-align:right;\">%1</p>")
                    .arg(QCoreApplication::translate("app::ui::Receipt", "Ajustement : %1")
                             .arg(formatMoney(data.adjustmentCents, data.currencySymbol)));
    }
    const long long payable = data.totalCents + data.adjustmentCents;
    html += QStringLiteral("<p style=\"text-align:right;font-size:%1pt;\"><b>%2</b></p>")
                .arg(fontPointSize + 2)
                .arg(QCoreApplication::translate("app::ui::Receipt", "Payable : %1")
                         .arg(formatMoney(payable, data.currencySymbol)));

    const QString footer = data.footerText.isEmpty()
                               ? QCoreApplication::translate("app::ui::Receipt",
                                                             "شكراً لتعاملكم معنا")
                               : data.footerText;
    html += QStringLiteral("<p style=\"text-align:center;font-size:%1pt;\">%2</p>")
                .arg(qMax(6, fontPointSize - 1))
                .arg(footer.toHtmlEscaped());

    html += QStringLiteral("</body></html>");

    auto* doc = new QTextDocument;
    QFont font;
    font.setPointSize(fontPointSize);
    doc->setDefaultFont(font);
    doc->setTextWidth(textWidth);
    doc->setHtml(html);
    if (paper == ReceiptPaper::Thermal58 || paper == ReceiptPaper::Thermal80) {
        // The document is given its own page: a single page whose height is
        // the text height. QTextDocument lays out its content into this page,
        // and QPrinter prints one page because there is only one.
        const qreal widthMm = (paper == ReceiptPaper::Thermal58) ? 58.0 : 80.0;
        const qreal heightMm = qMax<qreal>(100.0, doc->size().height() * 25.4 / 96.0 + 10.0);
        doc->setPageSize(QSizeF(widthMm * 96.0 / 25.4, heightMm * 96.0 / 25.4));
    }
    return doc;
}

ReceiptPaper currentReceiptPaper(app::data::Database& db)
{
    data::SettingRepository settings(db);
    const QString value = settings.value(QStringLiteral("receipt_paper_size")).value_or(QString());
    if (value == QStringLiteral("58")) {
        return ReceiptPaper::Thermal58;
    }
    if (value == QStringLiteral("80")) {
        return ReceiptPaper::Thermal80;
    }
    if (value == QStringLiteral("a5")) {
        return ReceiptPaper::A5;
    }
    if (value == QStringLiteral("a4")) {
        return ReceiptPaper::A4;
    }
    return ReceiptPaper::Thermal80;
}

QString paperSettingValue(ReceiptPaper paper)
{
    switch (paper) {
    case ReceiptPaper::Thermal58:
        return QStringLiteral("58");
    case ReceiptPaper::Thermal80:
        return QStringLiteral("80");
    case ReceiptPaper::A5:
        return QStringLiteral("a5");
    case ReceiptPaper::A4:
        return QStringLiteral("a4");
    }
    return QStringLiteral("80");
}

} // namespace app::ui
