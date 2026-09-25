#include "theme.h"

#include <QApplication>
#include <QFile>

namespace app::ui {

namespace {
QString g_activeTheme = QStringLiteral("light");
} // namespace

QString themesLightPath()
{
    return QStringLiteral(":/mahali/themes/light.qss");
}

QString themesDarkPath()
{
    return QStringLiteral(":/mahali/themes/dark.qss");
}

QString activeTheme()
{
    return g_activeTheme;
}

void applyTheme(const QString& key, QApplication& app)
{
    const QString path = key == QStringLiteral("dark")
                             ? themesDarkPath()
                             : themesLightPath();
    g_activeTheme = key == QStringLiteral("dark") ? QStringLiteral("dark")
                                                  : QStringLiteral("light");
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(file.readAll()));
    } else {
        app.setStyleSheet(QString());
    }
}

} // namespace app::ui