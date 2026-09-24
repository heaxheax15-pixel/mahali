#pragma once

#include <QString>
#include <optional>

namespace app::ui {

// Money lives in the codebase as integer cents. This header is the single place
// that renders or parses them as text. No currency symbol is hardcoded: the
// store's currency is a shop setting (Phase 14). The UI cache set on startup
// (and from the Settings page) appends the symbol to rendered amounts.

inline QString& liveCurrencySymbol()
{
    static QString symbol;
    return symbol;
}

inline void setCurrencySymbol(const QString& symbol)
{
    liveCurrencySymbol() = symbol.trimmed();
}

inline QString formatMoney(long long cents)
{
    const bool negative = cents < 0;
    const long long magnitude = qAbs<long long>(cents);
    const long long major = magnitude / 100;
    const QString minor = QString::number(magnitude % 100).rightJustified(2, QLatin1Char('0'));
    const QString body = QStringLiteral("%1.%2").arg(major).arg(minor);
    const QString sign = negative ? QStringLiteral("-") : QString();
    if (liveCurrencySymbol().isEmpty()) {
        return sign + body;
    }
    return sign + body + QLatin1Char(' ') + liveCurrencySymbol();
}

// Parses money typed in any shape into cents: "12", "12.5", "12,50",
// "1,234.50", "500 دج", "١٢٫٥٠" ... Accepts both '.' and ',' as the decimal
// separator by smart auto-detection:
//   - when both separators appear, the rightmost one is the decimal point;
//   - a lone separator followed by a 3-digit tail is a thousands separator
//     ("1,000" / "1.000" -> 1000), otherwise it is decimal ("12,5" -> 12.50).
// Negative values are rejected. Returns nullopt on junk.
inline std::optional<long long> parseMoney(const QString& text)
{
    QString cleaned = text.trimmed();
    if (cleaned.isEmpty()) {
        return std::nullopt;
    }
    if (cleaned.startsWith(QLatin1Char('-'))) {
        return std::nullopt;
    }

    // Normalize Arabic-Indic digits (٠١٢٣٤٥٦٧٨٩) and their separators
    // (٫ decimal, ٬ thousands).
    const QString arabicDigits = QStringLiteral("٠١٢٣٤٥٦٧٨٩");
    for (int i = 0; i < 10; ++i) {
        cleaned.replace(arabicDigits.at(i), QString::number(i));
    }
    cleaned.replace(QChar(0x066B), QLatin1Char('.'));
    cleaned.replace(QChar(0x066C), QLatin1Char(','));

    // Keep digits and separators only (currency words/symbols fall away).
    QString num;
    num.reserve(cleaned.size());
    for (const QChar& ch : cleaned) {
        if (ch.isDigit()) {
            num.append(ch);
        } else if (ch == QLatin1Char('.') || ch == QLatin1Char(',')) {
            num.append(ch);
        }
    }
    if (num.isEmpty()) {
        return std::nullopt;
    }

    const int lastDot = num.lastIndexOf(QLatin1Char('.'));
    const int lastComma = num.lastIndexOf(QLatin1Char(','));
    const int lastSep = qMax(lastDot, lastComma);

    bool hasDecimal = lastSep >= 0;
    if (hasDecimal) {
        const int tailLen = num.length() - lastSep - 1;
        // A lone separator with a 3-digit tail groups thousands: "1,000".
        if ((lastDot < 0 || lastComma < 0) && tailLen == 3 && lastSep > 0) {
            num.remove(QLatin1Char('.'));
            num.remove(QLatin1Char(','));
            hasDecimal = false;
        }
    }

    QString major = hasDecimal ? num.left(lastSep) : num;
    QString minor = hasDecimal ? num.mid(lastSep + 1) : QString();
    if (hasDecimal) {
        major.remove(QLatin1Char('.'));
        major.remove(QLatin1Char(','));
        minor.remove(QLatin1Char('.'));
        minor.remove(QLatin1Char(','));
    }
    if (major.isEmpty()) {
        major = QStringLiteral("0");
    }
    if (minor.isEmpty()) {
        minor = QStringLiteral("00");
    } else if (minor.size() == 1) {
        minor.append(QLatin1Char('0'));
    } else if (minor.size() > 2) {
        minor = minor.left(2);
    }

    bool majorOk = false;
    bool minorOk = false;
    const long long majorValue = major.toLongLong(&majorOk);
    long long minorValue = minor.toLongLong(&minorOk);
    if (!majorOk || !minorOk) {
        return std::nullopt;
    }
    if (minorValue >= 100) {
        minorValue %= 100;
    }
    return majorValue * 100 + minorValue;
}

inline QString formatBatch(long long quantity, const QString& unit)
{
    return unit.isEmpty() ? QString::number(quantity)
                          : QStringLiteral("%1 %2").arg(quantity).arg(unit);
}

} // namespace app::ui