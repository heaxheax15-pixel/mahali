#include "zakat_setting_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

ZakatSettingRepository::ZakatSettingRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::ZakatSetting settingFromQuery(const QSqlQuery& query)
{
    core::ZakatSetting setting;
    setting.id = query.value(0).toInt();
    setting.key = query.value(1).toString();
    setting.value = query.value(2).toString();
    return setting;
}

} // namespace

std::optional<core::ZakatSetting> ZakatSettingRepository::findByKey(const QString& key) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, key, value FROM zakat_settings WHERE key = ?"));
    query.addBindValue(key);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return settingFromQuery(query);
}

std::vector<core::ZakatSetting> ZakatSettingRepository::findAll() const
{
    std::vector<core::ZakatSetting> settings;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, key, value FROM zakat_settings ORDER BY key"));
    if (!query.exec()) {
        return settings;
    }
    while (query.next()) {
        settings.push_back(settingFromQuery(query));
    }
    return settings;
}

void ZakatSettingRepository::set(const QString& key, const QString& value)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO zakat_settings (key, value) VALUES (?, ?) "
                       "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(key);
    query.addBindValue(value);
    query.exec();
}

} // namespace app::data