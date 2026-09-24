#include "device_identity.h"

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
    db.beginTransaction();
    settings.set(kSettingKey, minted);
    const bool committed = db.commit();
    if (!committed) {
        db.rollback();
        // Persistence failed (disk full, locked file, ...): refuse a silently
        // unstable identity rather than risk ops under a different id later.
        return QString();
    }
    return minted;
}

} // namespace app::data