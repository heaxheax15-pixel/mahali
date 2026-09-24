#pragma once

#include <QDateTime>
#include <optional>
#include <vector>

#include "core/stock_movement.h"
#include "database.h"

namespace app::data {

class StockMovementRepository {
public:
    explicit StockMovementRepository(Database& db);

    std::optional<core::StockMovement> findById(int id) const;
    std::vector<core::StockMovement> findByProductId(int productId) const;
    std::vector<core::StockMovement> findBetween(const QDateTime& from, const QDateTime& to) const;
    long long sumByProductId(int productId) const;

    int insert(const core::StockMovement& movement);
    void reverse(int originalMovementId, const QString& reason);

private:
    Database& m_db;
};

} // namespace app::data