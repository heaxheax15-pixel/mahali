#pragma once

#include <QString>

namespace app::core {

struct Supplier {
    int id = 0;
    QString name = QStringLiteral("");
};

} // namespace app::core