#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "core/device.h"
#include "database.h"

namespace app::data {

class DeviceRepository {
public:
    explicit DeviceRepository(Database& db);

    std::optional<core::Device> findById(int id) const;
    std::optional<core::Device> findByDeviceId(const QString& deviceId) const;
    std::vector<core::Device> findAll() const;

    int save(const core::Device& device);
    void recordActivity(const core::Device& device);

private:
    Database& m_db;
};

} // namespace app::data