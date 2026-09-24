#pragma once

#include <QDateTime>
#include <optional>
#include <vector>

#include "core/sale.h"
#include "database.h"

namespace app::data {

class SaleRepository {
public:
    explicit SaleRepository(Database& db);

    std::optional<core::Sale> findById(int id) const;
    std::vector<core::Sale> findBetween(const QDateTime& from, const QDateTime& to) const;
    std::vector<core::Sale> findAll() const;
    int countByDeviceId(const QString& deviceId) const;

    int insert(const core::Sale& sale);

private:
    Database& m_db;
};

} // namespace app::data