#include "barcode_utils.h"

namespace app::core {
namespace {

// The AZERTY unshifted number row, in key order: index 0 is the 1 key, and
// index 9 is the 0 key, so a symbol's digit is (index + 1) % 10.
const QString kAzertyNumberRow = QStringLiteral("&é\"'(-è_çà");

} // namespace

QString normalizeScannedBarcode(const QString& input)
{
    bool hasAzertySymbol = false;
    for (const QChar c : input) {
        if (kAzertyNumberRow.indexOf(c) >= 0) {
            hasAzertySymbol = true;
            continue;
        }
        // ASCII digits only: QChar::isDigit() would also accept Arabic-Indic
        // digits, which no barcode carries and which must not be touched.
        if (c.unicode() < '0' || c.unicode() > '9') {
            return input;
        }
    }
    if (!hasAzertySymbol) {
        return input;
    }

    QString normalized = input;
    for (int i = 0; i < normalized.size(); ++i) {
        const int key = kAzertyNumberRow.indexOf(normalized[i]);
        if (key >= 0) {
            normalized[i] = QLatin1Char('0' + (key + 1) % 10);
        }
    }
    return normalized;
}

} // namespace app::core
