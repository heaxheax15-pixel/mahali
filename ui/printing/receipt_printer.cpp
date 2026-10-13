#include "receipt_printer.h"

#include <QFileDialog>
#include <QPageSize>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QSizeF>
#include <QTextDocument>
#include <QWidget>

#include <QCoreApplication>

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

bool printReceiptInteractive(QTextDocument* doc, ReceiptPaper paper, QWidget* parent)
{
    const QList<QPrinterInfo> printers = QPrinterInfo::availablePrinters();
    if (printers.isEmpty()) {
        QString path = QFileDialog::getSaveFileName(
            parent, QCoreApplication::translate("app::ui::Receipt",
                                                "Enregistrer le reçu en PDF"),
            QString(),
            QStringLiteral("*.pdf"));
        if (path.isEmpty()) {
            return false;
        }
        if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".pdf");
        }
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(path);
        configurePrinterForPaper(printer, paper);
        if (doc) {
            const QString title = doc->metaInformation(QTextDocument::DocumentTitle);
            if (!title.isEmpty()) {
                printer.setDocName(title);
            }
        }
        printReceiptToPrinter(doc, printer);
        return true;
    }

    QPrinter printer(QPrinter::HighResolution);
    configurePrinterForPaper(printer, paper);
    if (doc) {
        const QString title = doc->metaInformation(QTextDocument::DocumentTitle);
        if (!title.isEmpty()) {
            printer.setDocName(title);
        }
    }
    QPrintDialog dialog(&printer, parent);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }
    if (doc) {
        const QString title = doc->metaInformation(QTextDocument::DocumentTitle);
        if (!title.isEmpty()) {
            printer.setDocName(title);
        }
    }
    printReceiptToPrinter(doc, printer);
    return true;
}

} // namespace app::ui
