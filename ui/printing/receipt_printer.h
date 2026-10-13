#pragma once

#include <QString>

#include "receipt.h"

class QPrinter;
class QTextDocument;
class QWidget;

namespace app::ui {

// Sends a rendered receipt to the given printer. The page size is set on
// the printer before printing: a 58mm roll is 58mm wide and continuous,
// and the standard A4 page a laser printer defaults to would push the
// table off the right edge. The caller owns both arguments.
void printReceiptToPrinter(QTextDocument* doc, QPrinter& printer);

// Configures a printer for the paper chosen in settings and hands it to
// the caller. Used by the interactive dialog and by tests that pass
// OutputFormat::PdfFormat instead of a real printer.
void configurePrinterForPaper(QPrinter& printer, ReceiptPaper paper);

// Shows QPrintDialog (or, when no printer is installed, a save-as-PDF
// dialog) and prints. Returns true if the document was sent somewhere,
// false if the user cancelled. parent is used only for the dialog.
bool printReceiptInteractive(QTextDocument* doc, ReceiptPaper paper,
                             QWidget* parent);

} // namespace app::ui
