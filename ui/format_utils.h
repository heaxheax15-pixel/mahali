#pragma once

#include <QString>
#include <optional>

namespace app::ui {

// Money lives in the codebase as integer cents. This header is the single place
// that renders or parses them as text. No currency symbol is hardcoded: the
// store's currency is a shop setting (Phase 14).

inline QString formatMoney(long long cents)
{
    const bool negative = cents < 0;
    const long long magnitude = qAbs<long long>(cents);
    const long long major = magnitude / 100;
    const QString minor = QString::number(magnitude % 100).rightJustified(2, QLatin1Char('0'));
    const QString body = QStringLiteral("%1.%2").arg(major).arg(minor);
    return negative ? QStringLiteral("-") + body : body;
}

// Parses "1234.5", "1234", "1,234.50" ... into cents. Returns nullopt on junk.
inline std::optional<long long> parseMoney(const QString& text)
{
    QString cleaned = text.trimmed();
    if (cleaned.isEmpty()) {
        return std::nullopt;
    }
    cleaned.remove(QLatin1Char(','));
    cleaned.remove(QLatin1Char(' '));

    bool negative = cleaned.startsWith(QLatin1Char('-'));
    if (negative) {
        cleaned.remove(0, 1);
    }

    bool ok = false;
    const int dotIndex = cleaned.indexOf(QLatin1Char('.'));
    if (dotIndex < 0) {
        const long long major = cleaned.toLongLong(&ok);
        if (!ok) {
            return std::nullopt;
        }
        return negative ? -major * 100 : major * 100;
    }

    const QString majorPart = cleaned.left(dotIndex);
    QString minorPart = cleaned.mid(dotIndex + 1);
    if (minorPart.length() > 2) {
        minorPart = minorPart.left(2);
    }
    while (minorPart.length() < 2) {
        minorPart.append(QLatin1Char('0'));
    }
    const long long major = majorPart.toLongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    const long long minor = minorPart.toLongLong(&ok);
    if (!ok) {
        return std::nullopt;
    }
    const long long cents = major * 100 + minor;
    return negative ? -cents : cents;
}

inline QString formatBatch(long long quantity, const QString& unit)
{
    return unit.isEmpty() ? QString::number(quantity)
                          : QStringLiteral("%1 %2").arg(quantity).arg(unit);
}

} // namespace app::ui