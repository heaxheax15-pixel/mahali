#include "receipt_printer.h"

#include <QCoreApplication>
#include <QDir>
#include <QPageSize>
#include <QPrinter>
#include <QPrinterInfo>
#include <QSizeF>
#include <QStandardPaths>
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
    // Height is set on the document; sync printer page size from document.
    if (paper == ReceiptPaper::Thermal58 || paper == ReceiptPaper::Thermal80) {
        const QSizeF docSize = doc->pageSize();
        if (docSize.isValid()) {
            const QSizeF mmSize(docSize.width() * 25.4 / 96.0,
                                docSize.height() * 25.4 / 96.0);
            printer.setPageSize(QPageSize(mmSize, QPageSize::Millimeter,
                                          QStringLiteral("receipt"),
                                          QPageSize::ExactMatch));
            printer.setFullPage(true);
        } else {
            // Fallback if doc page size not set
            const qreal widthMm = (paper == ReceiptPaper::Thermal58) ? 58.0 : 80.0;
            const qreal contentHeightPx = doc ? doc->size().height() : 0;
            const qreal heightMm = qMax<qreal>(100.0,
                contentHeightPx * 25.4 / 96.0 + 10.0);
            printer.setPageSize(QPageSize(QSizeF(widthMm, heightMm),
                                          QPageSize::Millimeter,
                                          QStringLiteral("receipt"),
                                          QPageSize::ExactMatch));
            printer.setFullPage(true);
        }
    }

    printReceiptToPrinter(doc, printer);

    if (!QPrinterInfo::defaultPrinter().isNull()) {
        QPrinter paperPrinter(QPrinter::HighResolution);
        configurePrinterForPaper(paperPrinter, paper);
        if (paper == ReceiptPaper::Thermal58 || paper == ReceiptPaper::Thermal80) {
            const QSizeF docSize = doc->pageSize();
            if (docSize.isValid()) {
                const QSizeF mmSize(docSize.width() * 25.4 / 96.0,
                                    docSize.height() * 25.4 / 96.0);
                paperPrinter.setPageSize(QPageSize(mmSize, QPageSize::Millimeter,
                                                   QStringLiteral("receipt"),
                                                   QPageSize::ExactMatch));
                paperPrinter.setFullPage(true);
            } else {
                const qreal widthMm = (paper == ReceiptPaper::Thermal58) ? 58.0 : 80.0;
                const qreal contentHeightPx = doc ? doc->size().height() : 0;
                const qreal heightMm = qMax<qreal>(100.0,
                    contentHeightPx * 25.4 / 96.0 + 10.0);
                paperPrinter.setPageSize(QPageSize(QSizeF(widthMm, heightMm),
                                                   QPageSize::Millimeter,
                                                   QStringLiteral("receipt"),
                                                   QPageSize::ExactMatch));
                paperPrinter.setFullPage(true);
            }
        }
        printReceiptToPrinter(doc, paperPrinter);
    }

    return path;
}

} // namespace app::ui
