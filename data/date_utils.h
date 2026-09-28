#pragma once

#include <QDateTime>
#include <QString>
#include <optional>

namespace app::data {

inline QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}

inline QString toIso(const QDateTime& value)
{
    return value.toString(Qt::ISODateWithMs);
}

inline std::optional<QDateTime> fromIso(const QString& value)
{
    if (value.isEmpty()) {
        return std::nullopt;
    }
    const QDateTime parsed = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        return std::nullopt;
    }
    return parsed;
}

// Stored timestamps are ISO text, and an inclusive window is two string
// comparisons. A bound written as a plain date names a whole day, but "2026-03-31"
// sorts before "2026-03-31T10:00:00.000", so a bound left as a date silently drops
// everything later in that final day. Both helpers widen a date-only bound to
// cover the whole day, and leave a bound that already carries a time alone.
inline QString widenToDayStart(const QString& value)
{
    if (value.contains(QLatin1Char('T'))) {
        return value;
    }
    return value + QStringLiteral("T00:00:00.000");
}

inline QString widenToDayEnd(const QString& value)
{
    if (value.contains(QLatin1Char('T'))) {
        return value;
    }
    return value + QStringLiteral("T23:59:59.999");
}

inline std::optional<long long> toLongLong(const QVariant& value)
{
    if (value.isNull()) {
        return std::nullopt;
    }
    return value.toLongLong();
}

} // namespace app::data