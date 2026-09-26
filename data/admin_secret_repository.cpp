#include "admin_secret_repository.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QSqlQuery>

namespace app::data {

namespace {

QByteArray randomSalt()
{
    QByteArray salt(16, 0);
    for (int i = 0; i < 16; ++i) {
        salt[i] = static_cast<char>(QRandomGenerator::system()->generate() & 0xFF);
    }
    return salt;
}

QByteArray deriveKey(const QString& password, const QByteArray& salt)
{
    return QPasswordDigestor::deriveKeyPbkdf2(
        QCryptographicHash::Sha256, password.toUtf8(), salt, 100000, 32);
}

QString toHex(const QByteArray& data)
{
    return QString::fromLatin1(data.toHex());
}

QByteArray fromHex(const QString& hex)
{
    return QByteArray::fromHex(hex.toLatin1());
}

} // namespace

AdminSecretRepository::AdminSecretRepository(Database& db)
    : m_db(db)
{
}

bool AdminSecretRepository::setMaster(int userId, const QString& password)
{
    const QByteArray salt = randomSalt();
    const QByteArray hash = deriveKey(password, salt);

    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT OR REPLACE INTO admin_secrets (user_id, master_hash, master_salt) VALUES (?, ?, ?)"));
    query.addBindValue(userId);
    query.addBindValue(toHex(hash));
    query.addBindValue(toHex(salt));
    return query.exec();
}

bool AdminSecretRepository::verifyMaster(int userId, const QString& password) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT master_hash, master_salt FROM admin_secrets WHERE user_id = ?"));
    query.addBindValue(userId);
    if (!query.exec() || !query.next()) {
        return false;
    }
    const QString storedHashHex = query.value(0).toString();
    const QString storedSaltHex = query.value(1).toString();

    const QByteArray salt = fromHex(storedSaltHex);
    const QByteArray derived = deriveKey(password, salt);
    return toHex(derived) == storedHashHex;
}

std::optional<int> AdminSecretRepository::findAdminByMaster(const QString& password) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT user_id, master_hash, master_salt FROM admin_secrets"));
    if (!query.exec()) {
        return std::nullopt;
    }
    while (query.next()) {
        const int userId = query.value(0).toInt();
        const QString storedHashHex = query.value(1).toString();
        const QString storedSaltHex = query.value(2).toString();

        const QByteArray salt = fromHex(storedSaltHex);
        const QByteArray derived = deriveKey(password, salt);
        if (toHex(derived) == storedHashHex) {
            return userId;
        }
    }
    return std::nullopt;
}

} // namespace app::data