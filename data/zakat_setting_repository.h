#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "core/zakat_setting.h"
#include "database.h"

namespace app::data {

class ZakatSettingRepository {
public:
    explicit ZakatSettingRepository(Database& db);

    std::optional<core::ZakatSetting> findByKey(const QString& key) const;
    std::vector<core::ZakatSetting> findAll() const;

    void set(const QString& key, const QString& value);

private:
    Database& m_db;
};

} // namespace app::data