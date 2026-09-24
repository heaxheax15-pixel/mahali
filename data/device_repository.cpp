#include "device_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

DeviceRepository::DeviceRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::Device deviceFromQuery(const QSqlQuery& query)
{
    core::Device device;
    device.id = query.value(0).toInt();
    device.deviceId = query.value(1).toString();
    device.authToken = query.value(2).toString();
    device.pairedAt = fromIso(query.value(3).toString()).value_or(QDateTime());
    device.lastSeenAt = fromIso(query.value(4).toString()).value_or(QDateTime());
    return device;
}

} // namespace

std::optional<core::Device> DeviceRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, device_id, auth_token, paired_at, last_seen_at FROM devices WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return deviceFromQuery(query);
}

std::optional<core::Device> DeviceRepository::findByDeviceId(const QString& deviceId) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT id, device_id, auth_token, paired_at, last_seen_at FROM devices WHERE device_id = ?"));
    query.addBindValue(deviceId);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return deviceFromQuery(query);
}

std::vector<core::Device> DeviceRepository::findAll() const
{
    std::vector<core::Device> devices;
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT id, device_id, auth_token, paired_at, last_seen_at FROM devices ORDER BY paired_at"));
    if (!query.exec()) {
        return devices;
    }
    while (query.next()) {
        devices.push_back(deviceFromQuery(query));
    }
    return devices;
}

int DeviceRepository::save(const core::Device& device)
{
    QSqlQuery query(m_db.handle());
    if (device.id == 0) {
        query.prepare(QStringLiteral("INSERT INTO devices (device_id, auth_token, paired_at, last_seen_at) "
                                     "VALUES (?, ?, ?, ?)"));
        query.addBindValue(device.deviceId);
        query.addBindValue(device.authToken);
        query.addBindValue(toIso(device.pairedAt.isValid() ? device.pairedAt : QDateTime::currentDateTime()));
        query.addBindValue(device.lastSeenAt.isValid() ? toIso(device.lastSeenAt) : QVariant());
        if (!query.exec()) {
            return 0;
        }
        return query.lastInsertId().toInt();
    }

    query.prepare(QStringLiteral("UPDATE devices SET device_id = ?, auth_token = ?, paired_at = ?, last_seen_at = ? "
                                 "WHERE id = ?"));
    query.addBindValue(device.deviceId);
    query.addBindValue(device.authToken);
    query.addBindValue(toIso(device.pairedAt.isValid() ? device.pairedAt : QDateTime::currentDateTime()));
    query.addBindValue(device.lastSeenAt.isValid() ? toIso(device.lastSeenAt) : QVariant());
    query.addBindValue(device.id);
    if (!query.exec()) {
        return 0;
    }
    return device.id;
}

void DeviceRepository::recordActivity(const core::Device& device)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("UPDATE devices SET last_seen_at = ? WHERE id = ?"));
    query.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    query.addBindValue(device.id);
    query.exec();
}

} // namespace app::data