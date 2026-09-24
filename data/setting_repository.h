#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "core/setting.h"
#include "database.h"

namespace app::data {

class SettingRepository {
public:
    explicit SettingRepository(Database& db);

    std::optional<core::Setting> findByKey(const QString& key) const;
    std::optional<QString> value(const QString& key) const;
    std::vector<core::Setting> findAll() const;

    void set(const QString& key, const QString& value);

private:
    Database& m_db;
};

} // namespace app::data