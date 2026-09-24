#include "setting_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

SettingRepository::SettingRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::Setting settingFromQuery(const QSqlQuery& query)
{
    core::Setting setting;
    setting.id = query.value(0).toInt();
    setting.key = query.value(1).toString();
    setting.value = query.value(2).toString();
    return setting;
}

} // namespace

std::optional<core::Setting> SettingRepository::findByKey(const QString& key) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, key, value FROM settings WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return settingFromQuery(query);
}

std::optional<QString> SettingRepository::value(const QString& key) const
{
    const std::optional<core::Setting> setting = findByKey(key);
    if (!setting.has_value()) {
        return std::nullopt;
    }
    return setting->value;
}

std::vector<core::Setting> SettingRepository::findAll() const
{
    std::vector<core::Setting> settings;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, key, value FROM settings ORDER BY key"));
    if (!query.exec()) {
        return settings;
    }
    while (query.next()) {
        settings.push_back(settingFromQuery(query));
    }
    return settings;
}

void SettingRepository::set(const QString& key, const QString& value)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO settings (key, value) VALUES (?, ?) "
                       "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(key);
    query.addBindValue(value);
    query.exec();
}

} // namespace app::data