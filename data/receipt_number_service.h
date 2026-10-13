#pragma once

#include <QString>

namespace app::data {
class Database;
}

namespace app::data {

// Mints the next receipt number for the current calendar year. MUST be
// called inside an open transaction: the counter it increments lives in
// receipt_sequences, and the increment and the sale that uses the number
// have to commit together, or a crash between them either loses a number
// or issues the same one twice. Format: "<prefix>-<YYYY>-<NNNN>".
QString mintReceiptNumber(Database& db);

// Reads settings.device_prefix, defaulting to "A" when the key is absent.
// Exposed so the migration and the printer agree on the same prefix without
// repeating the fallback.
QString currentDevicePrefix(Database& db);

} // namespace app::data
