#pragma once

#include <QString>

namespace app::core {

struct Customer {
    int id = 0;
    QString name = QStringLiteral("");
    QString phone = QStringLiteral("");
};

} // namespace app::core