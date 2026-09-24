#pragma once

#include <QString>

namespace app::core {

struct Setting {
    int id = 0;
    QString key = QStringLiteral("");
    QString value = QStringLiteral("");
};

} // namespace app::core