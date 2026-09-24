#pragma once

#include <QDateTime>
#include <QString>

namespace app::core {

struct AuditLogEntry {
    int id = 0;
    QString actor = QStringLiteral("");
    QString action = QStringLiteral("");
    QString target = QStringLiteral("");
    QDateTime createdAt;
};

} // namespace app::core