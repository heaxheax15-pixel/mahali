#pragma once

#include <QString>

namespace app::core {

struct User {
    int id = 0;
    QString name = QStringLiteral("");
    QString role = QStringLiteral("");
    QString pin = QStringLiteral("");
    bool active = true;
};

} // namespace app::core