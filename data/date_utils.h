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

inline std::optional<long long> toLongLong(const QVariant& value)
{
    if (value.isNull()) {
        return std::nullopt;
    }
    return value.toLongLong();
}

} // namespace app::data