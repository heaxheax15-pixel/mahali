#include "sync_key.h"

#include <QRandomGenerator>

#include "setting_repository.h"

namespace app::data {

const QString SyncKey::kSettingKey = QStringLiteral("sync_hmac_key");

QByteArray SyncKey::ensure(Database& db)
{
    SettingRepository settings(db);
    const std::optional<QString> stored = settings.value(kSettingKey);
    if (stored.has_value() && !stored->isEmpty()) {
        return stored->toUtf8();
    }

    // 32 bytes from the OS CSPRNG (getrandom on Linux, BCryptGenRandom on
    // Windows), hex-encoded: 64 lowercase characters that survive a
    // hand-copy between machines without the punctuation problems base64
    // has in shell commands and support chats. The hex text is the key --
    // peers compare exactly these bytes, so the encoding only has to be
    // identical everywhere, not compact.
    QByteArray raw(32, '\0');
    QRandomGenerator* rng = QRandomGenerator::system();
    for (int i = 0; i < raw.size(); i += static_cast<int>(sizeof(quint64))) {
        const quint64 word = rng->generate64();
        for (int b = 0; b < static_cast<int>(sizeof(quint64)); ++b) {
            raw[i + b] = static_cast<char>((word >> (8 * b)) & 0xFF);
        }
    }

    const QString hex = QString::fromLatin1(raw.toHex());
    settings.set(kSettingKey, hex);
    return hex.toUtf8();
}

} // namespace app::data
