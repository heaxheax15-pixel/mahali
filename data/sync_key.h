#pragma once

#include <QByteArray>
#include <QString>

#include "database.h"

namespace app::data {

// The HMAC secret every machine on the shop LAN signs sync batches with.
// Minted on first use and persisted in the settings table, exactly like
// DeviceIdentity: a key that only lives in a header default is a key every
// installation in the world shares, and any device that knows the shipped
// string can post batches to any shop's server.
class SyncKey {
public:
    // Returns this database's sync key, minting and persisting a random one
    // when the setting is absent or empty. An existing non-empty value is
    // never replaced: rotating the secret behind the operator's back signs
    // with the new value while the peer still verifies with the old one, and
    // every batch then fails until somebody notices.
    static QByteArray ensure(Database& db);

    static const QString kSettingKey;
};

} // namespace app::data
