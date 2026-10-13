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

// The context the receipt's own labels are translated under. The renderer is a
// set of free functions, so tr() has no class to hang off; the tests look the
// strings up with this same context.
const char* const kReceiptContext = "app::ui::Receipt";

QString receiptText(const char* source)
{
    return QCoreApplication::translate(kReceiptContext, source);
}

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
    if (unitKind == QStringLiteral("package")) {
        return translatedPackageName(packageName);
    }
    return QCoreApplication::translate("app::ui::PosPage", "قطعة");
}

// unitPriceCents * quantity, or 0 when the product would not fit in a long
// long. A receipt is not a place to refuse: an impossible line is dropped to a
// zero total and the rest of the paper still prints.
long long lineTotalCentsFor(long long unitPriceCents, long long quantity)
{
    if (data::detail::productFits(unitPriceCents, quantity)) {
        return unitPriceCents * quantity;
    }
    return 0;
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
        line.lineTotalCents = lineTotalCentsFor(item.unitPriceCents, item.quantity);
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
        line.lineTotalCents = lineTotalCentsFor(item.unitPriceCents, item.quantity);
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
                .arg(receiptText("Reçu N° : %1").arg(data.receiptNumber.toHtmlEscaped()));
    html += QStringLiteral("<p>%1</p>")
                .arg(QLocale().toString(data.dateTime, QLocale::ShortFormat).toHtmlEscaped());
    if (!data.customerName.isEmpty()) {
        html += QStringLiteral("<p>%1</p>")
                    .arg(receiptText("Client : %1").arg(data.customerName.toHtmlEscaped()));
    }

    html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"2\"><tr>");
    html += QStringLiteral("<th align=\"left\">%1</th>").arg(receiptText("Nom"));
    html += QStringLiteral("<th align=\"right\">%1</th>").arg(receiptText("Qté"));
    if (!narrow) {
        html += QStringLiteral("<th align=\"left\">%1</th>").arg(receiptText("Unité"));
    }
    html += QStringLiteral("<th align=\"right\">%1</th>").arg(receiptText("Prix"));
    html += QStringLiteral("<th align=\"right\">%1</th>").arg(receiptText("Total"));
    html += QStringLiteral("</tr>");
    for (const ReceiptLine& line : data.lines) {
        html += QStringLiteral("<tr>");
        html += QStringLiteral("<td>%1</td>").arg(line.productName.toHtmlEscaped());
        html += QStringLiteral("<td align=\"right\">%1</td>").arg(line.quantity);
        if (!narrow) {
            html += QStringLiteral("<td>%1</td>").arg(line.unitLabel.toHtmlEscaped());
        }
        html += QStringLiteral("<td align=\"right\">%1</td>")
                    .arg(formatMoney(line.unitPriceCents, data.currencySymbol));
        html += QStringLiteral("<td align=\"right\">%1</td>")
                    .arg(formatMoney(line.lineTotalCents, data.currencySymbol));
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");

    html += QStringLiteral("<hr/>");

    html += QStringLiteral("<p style=\"text-align:right;\">%1</p>")
                .arg(receiptText("Total : %1").arg(formatMoney(data.totalCents, data.currencySymbol)));
    if (data.adjustmentCents != 0) {
        html += QStringLiteral("<p style=\"text-align:right;\">%1</p>")
                    .arg(receiptText("Ajustement : %1")
                             .arg(formatMoney(data.adjustmentCents, data.currencySymbol)));
    }
    const long long payable = data.totalCents + data.adjustmentCents;
    html += QStringLiteral("<p style=\"text-align:right;font-size:%1pt;\"><b>%2</b></p>")
                .arg(fontPointSize + 2)
                .arg(receiptText("Payable : %1").arg(formatMoney(payable, data.currencySymbol)));

    const QString footer = data.footerText.isEmpty()
                               ? receiptText("شكراً لتعاملكم معنا")
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
    return doc;
}

} // namespace app::ui
