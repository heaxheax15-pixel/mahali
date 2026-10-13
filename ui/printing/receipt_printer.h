#pragma once

#include <QString>

#include "receipt.h"

class QPrinter;
class QTextDocument;

namespace app::ui {

// Sends a rendered receipt to the given printer. The page size is set on
// the printer before printing: a 58mm roll is 58mm wide and continuous,
// and the standard A4 page a laser printer defaults to would push the
// table off the right edge. The caller owns both arguments.
void printReceiptToPrinter(QTextDocument* doc, QPrinter& printer);

// Configures a printer for the paper chosen in settings and hands it to
// the caller. Used by printAndSaveReceipt and by tests that pass
// OutputFormat::PdfFormat instead of a real printer.
void configurePrinterForPaper(QPrinter& printer, ReceiptPaper paper);

// Saves the receipt as a PDF inside the shop's receipts folder, and also
// prints a paper copy when the machine has a printer. Returns the path
// of the saved PDF, or an empty string on failure. Never opens a dialog:
// the cashier pressed one button and expects a paper slip, not a
// question.
QString printAndSaveReceipt(QTextDocument* doc, ReceiptPaper paper,
                            app::data::Database& db, QString* error);

} // namespace app::ui
