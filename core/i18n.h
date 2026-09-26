#pragma once

#include <QString>

namespace app::data {
class Database;
} // namespace app::data

namespace app::core {

// Language codes supported by the shipping translations.
QStringList supportedLanguages();

// The language used when nothing has been persisted yet.
QString defaultLanguage();

// Installs the application translator for `code` ("ar", "fr" or "en") from
// :/i18n/mahali_<code>.qm, plus Qt's own qtbase_<code>.qm, and pins the
// layout direction (ar -> RTL, fr/en -> LTR). Any previously installed
// translator is removed first, so calling this repeatedly is safe.
// Returns true when the application catalogue was found and installed.
//
// Note: .cpp lives in the mahali-data target, because reading the persisted
// choice needs SettingRepository and data depends on core, not the reverse.
bool applyLanguage(const QString& code);

// Reads the "language" setting, falling back to defaultLanguage().
QString currentLanguage(app::data::Database& db);

} // namespace app::core
