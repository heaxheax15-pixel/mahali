#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct Device {
    int id = 0;
    QString deviceId = QStringLiteral("");
    QString authToken = QStringLiteral("");
    QDateTime pairedAt;
    QDateTime lastSeenAt;
};

} // namespace app::core