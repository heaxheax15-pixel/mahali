#include "version.h"

#include <QStringList>

namespace app::core {

Version currentVersion()
{
    return parseVersion(QStringLiteral(MAHALI_VERSION));
}

bool isNewer(const Version& a, const Version& b)
{
    if (a.major != b.major) {
        return a.major > b.major;
    }
    if (a.minor != b.minor) {
        return a.minor > b.minor;
    }
    return a.patch > b.patch;
}

Version parseVersion(const QString& tag)
{
    QString text = tag.trimmed();
    if (text.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        text = text.mid(1);
    }
    // Release tags sometimes carry a suffix ("1.2.3-rc1"); only the leading
    // run of digits and dots counts, and only the first three components are
    // kept.
    int end = 0;
    while (end < text.size() && (text.at(end).isDigit() || text.at(end) == QLatin1Char('.'))) {
        ++end;
    }
    text = text.left(end);

    const QStringList parts = text.split(QLatin1Char('.'));
    if (parts.isEmpty() || parts.first().isEmpty()) {
        return Version{};
    }

    Version version;
    version.major = parts.at(0).toInt();
    if (parts.size() > 1) {
        version.minor = parts.at(1).toInt();
    }
    if (parts.size() > 2) {
        version.patch = parts.at(2).toInt();
    }
    return version;
}

} // namespace app::core
