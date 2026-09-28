#include "device_identity.h"

#include <QDebug>
#include <QUuid>

#include "setting_repository.h"

namespace app::data {

const QString DeviceIdentity::kSettingKey = QStringLiteral("device_id");

QString DeviceIdentity::ensure(Database& db)
{
    SettingRepository settings(db);
    if (const auto existing = settings.value(kSettingKey); existing.has_value() && !existing->isEmpty()) {
        return *existing;
    }

    const QString minted = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Checked like every other writer in this project. A BEGIN that fails means
    // a transaction is already open on this connection, and the commit below
    // would then close somebody else's transaction while this id was written
    // outside it, so nothing is written at all. lastError() is already set.
    if (!db.beginTransaction()) {
        qWarning() << "device identity was not minted: cannot begin a transaction:" << db.lastError();
        return QString();
    }
    settings.set(kSettingKey, minted);
    if (!db.commit()) {
        db.rollback();
        // Persistence failed (disk full, locked file, ...): refuse a silently
        // unstable identity rather than risk ops under a different id later.
        qWarning() << "device identity was not persisted:" << db.lastError();
        return QString();
    }
    return minted;
}

} // namespace app::data