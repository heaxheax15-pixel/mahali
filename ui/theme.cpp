#include "theme.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontDatabase>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>

#include <array>

#include "theme_tokens.h"

namespace app::ui {

namespace {
QString g_activeTheme = QStringLiteral("light");
bool g_fontsLoaded = false;
bool g_fusionSelected = false;

QString loadThemeStylesheet(const QString& path, const themeTokens::ThemeColors& colors)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QString stylesheet = QString::fromUtf8(file.readAll());
    const auto hex = [](quint32 rgba) { return QColor::fromRgba(rgba).name(); };
    stylesheet.replace(QStringLiteral("@WINDOW@"), hex(colors.window));
    stylesheet.replace(QStringLiteral("@SURFACE@"), hex(colors.surface));
    stylesheet.replace(QStringLiteral("@BORDER@"), hex(colors.border));
    stylesheet.replace(QStringLiteral("@TEXT@"), hex(colors.text));
    stylesheet.replace(QStringLiteral("@TEXT_SECONDARY@"), hex(colors.textSecondary));
    stylesheet.replace(QStringLiteral("@MUTED@"), hex(colors.muted));
    stylesheet.replace(QStringLiteral("@ACCENT@"), hex(colors.accent));
    stylesheet.replace(QStringLiteral("@ACCENT_SOFT@"), hex(colors.accentSoft));
    stylesheet.replace(QStringLiteral("@POSITIVE@"), hex(colors.positive));
    stylesheet.replace(QStringLiteral("@NEGATIVE@"), hex(colors.negative));
    stylesheet.replace(QStringLiteral("@WARNING@"), hex(colors.warning));
    stylesheet.replace(QStringLiteral("@POSITIVE_SOFT@"), hex(colors.positiveSoft));
    stylesheet.replace(QStringLiteral("@NEGATIVE_SOFT@"), hex(colors.negativeSoft));
    stylesheet.replace(QStringLiteral("%1"), QString::number(themeTokens::fontBodyPt));
    stylesheet.replace(QStringLiteral("%2"), QString::number(themeTokens::fontHeadingPt));
    stylesheet.replace(QStringLiteral("%3"), QString::number(themeTokens::fontNotePt));
    return stylesheet;
}

const QString& cachedThemeStylesheet(bool dark)
{
    static const std::array<QString, 2> stylesheets = {
        loadThemeStylesheet(themesLightPath(), themeTokens::lightColors),
        loadThemeStylesheet(themesDarkPath(), themeTokens::darkColors),
    };
    return stylesheets[dark ? 1 : 0];
}

void loadApplicationFont(QApplication& app)
{
    if (g_fontsLoaded) {
        return;
    }
    const int regularId = QFontDatabase::addApplicationFont(
        QStringLiteral(":/mahali/fonts/NotoSansArabic-Regular.ttf"));
    QFontDatabase::addApplicationFont(
        QStringLiteral(":/mahali/fonts/NotoSansArabic-Bold.ttf"));
    if (regularId >= 0) {
        const QStringList families = QFontDatabase::applicationFontFamilies(regularId);
        if (!families.isEmpty()) {
            QFont font(families.first());
            font.setPointSize(themeTokens::fontBodyPt);
            app.setFont(font);
        }
    }
    g_fontsLoaded = true;
}

void applyPalette(const QString& key, QApplication& app)
{
    const themeTokens::ThemeColors& colors = key == QStringLiteral("dark")
        ? themeTokens::darkColors
        : themeTokens::lightColors;
    const auto color = [](quint32 rgba) { return QColor::fromRgba(rgba); };

    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, color(colors.window));
    palette.setColor(QPalette::WindowText, color(colors.text));
    palette.setColor(QPalette::Base, color(colors.surface));
    palette.setColor(QPalette::AlternateBase, color(colors.window));
    palette.setColor(QPalette::Text, color(colors.text));
    palette.setColor(QPalette::Button, color(colors.surface));
    palette.setColor(QPalette::ButtonText, color(colors.text));
    palette.setColor(QPalette::Mid, color(colors.border));
    palette.setColor(QPalette::Highlight, color(colors.accent));
    palette.setColor(QPalette::HighlightedText, QColor(Qt::white));
    palette.setColor(QPalette::PlaceholderText, color(colors.muted));
    app.setPalette(palette);
}
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
    const bool dark = key == QStringLiteral("dark");
    g_activeTheme = dark ? QStringLiteral("dark") : QStringLiteral("light");
    if (!g_fusionSelected) {
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        g_fusionSelected = true;
    }
    loadApplicationFont(app);
    applyPalette(g_activeTheme, app);
    app.setStyleSheet(cachedThemeStylesheet(dark));
}

} // namespace app::ui