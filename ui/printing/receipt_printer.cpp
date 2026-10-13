#include "receipt_printer.h"

#include <QCoreApplication>
#include <QDir>
#include <QPageSize>
#include <QPrinter>
#include <QPrinterInfo>
#include <QStandardPaths>
#include <QSizeF>
#include <QTextDocument>

#include "data/setting_repository.h"

namespace app::ui {

void configurePrinterForPaper(QPrinter& printer, ReceiptPaper paper)
{
    switch (paper) {
    case ReceiptPaper::Thermal58: {
        // A 58mm roll is exactly the paper width, not A4. Keeping full-page
        // true and the custom page size matches the behaviour of the small
        // POS printers this grocery expects.
        printer.setPageSize(QPageSize(QSizeF(58, 297), QPageSize::Millimeter,
                                      QStringLiteral("58mm roll"), QPageSize::ExactMatch));
        printer.setFullPage(true);
        break;
    }
    case ReceiptPaper::Thermal80: {
        printer.setPageSize(QPageSize(QSizeF(80, 297), QPageSize::Millimeter,
                                      QStringLiteral("80mm roll"), QPageSize::ExactMatch));
        printer.setFullPage(true);
        break;
    }
    case ReceiptPaper::A5: {
        printer.setPageSize(QPageSize(QPageSize::A5));
        printer.setFullPage(false);
        break;
    }
    case ReceiptPaper::A4: {
        printer.setPageSize(QPageSize(QPageSize::A4));
        printer.setFullPage(false);
        break;
    }
    default: {
        printer.setPageSize(QPageSize(QSizeF(80, 297), QPageSize::Millimeter,
                                      QStringLiteral("80mm roll"), QPageSize::ExactMatch));
        printer.setFullPage(true);
        break;
    }
    }

    printer.setDocName(QStringLiteral("Mahali receipt"));
}

void printReceiptToPrinter(QTextDocument* doc, QPrinter& printer)
{
    // QTextDocument::print takes a QPrinter* and is the only place in the
    // app that touches QtPrintSupport directly.
    if (doc) {
        doc->print(&printer);
    }
}

QString printAndSaveReceipt(QTextDocument* doc, ReceiptPaper paper,
                            app::data::Database& db, QString* error)
{
    if (error) {
        error->clear();
    }

    app::data::SettingRepository settings(db);
    QString folder = settings.value(QStringLiteral("receipt_save_folder")).value_or(QString());
    if (folder.isEmpty()) {
        // First run: fall back to a default that is easy to find, and store it
        // so the next call is one query instead of a guess -- and so the field
        // in Settings shows it and the shopkeeper can move the folder.
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                 + QStringLiteral("/Mahali");
        settings.set(QStringLiteral("receipt_save_folder"), folder);
    }

    if (!QDir().mkpath(folder)) {
        if (error) {
            *error = QCoreApplication::translate("app::ui::Receipt",
                                                 "تعذّر إنشاء مجلد الإيصالات");
        }
        return QString();
    }

    QString base;
    if (doc) {
        base = doc->metaInformation(QTextDocument::DocumentTitle);
    }
    if (base.isEmpty()) {
        base = QStringLiteral("receipt");
    }
    const QString path = folder + QLatin1Char('/') + base + QStringLiteral(".pdf");

    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(path);
    configurePrinterForPaper(printer, paper);
    printReceiptToPrinter(doc, printer);

    if (!QPrinterInfo::defaultPrinter().isNull()) {
        QPrinter paperPrinter(QPrinter::HighResolution);
        configurePrinterForPaper(paperPrinter, paper);
        // Full-page for thermal rolls means the content uses the whole
        // 58mm/80mm width, which is the point of a receipt roll; a printer
        // that demands A4 will refuse the custom page size and Qt falls
        // back to its own — the receipt renders, only smaller.
        printReceiptToPrinter(doc, paperPrinter);
    }

    return path;
}

} // namespace app::ui
