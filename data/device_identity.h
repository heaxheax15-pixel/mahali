#pragma once

#include <QString>

#include "database.h"

namespace app::data {

// The device's own stable identity on the wire. The central Windows DB records
// applied operations under this id, so it must never change once minted: the
// device derives it on first use and persists it in its settings table. A
// power cut between minting and persisting is harmless — the next start simply
// mints again, and nothing has been synced before then.
class DeviceIdentity {
public:
    // Returns this database's device id, minting and persisting one on first use.
    static QString ensure(Database& db);

    static const QString kSettingKey;
};

} // namespace app::data