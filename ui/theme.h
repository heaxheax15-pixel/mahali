#pragma once

#include <QString>

class QApplication;

namespace app::ui {

QString themesLightPath();
QString themesDarkPath();

// Returns the current theme key ("light" or "dark").
QString activeTheme();

// Reads :/mahali/themes/<key>.qss and applies it to the given QApplication.
// Falls back to the light theme when <key> is unknown.
void applyTheme(const QString& key, QApplication& app);

} // namespace app::ui