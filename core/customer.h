#pragma once

#include <QString>

namespace app::core {

struct Customer {
    int id = 0;
    QString name = QStringLiteral("");
    QString phone = QStringLiteral("");
    // What they already owed when their account was opened. Added on top of the
    // ledger so a balance carried in from a previous book is not lost when the
    // first line is written.
    long long openingBalanceCents = 0;
    // A customer that is no longer served: kept, so the history stays readable,
    // but hidden from the default "Tous" filter.
    bool active = true;
};

} // namespace app::core