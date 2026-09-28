#pragma once

#include <optional>
#include <vector>

#include "core/purchase.h"
#include "database.h"

namespace app::data {

class PurchaseRepository {
public:
    explicit PurchaseRepository(Database& db);

    std::optional<core::Purchase> findById(int id) const;
    QVector<core::Purchase> findBySupplier(int supplierId) const;
    QVector<core::Purchase> findBetween(const QString& fromIso, const QString& toIso) const;
    QVector<core::Purchase> findAll() const;

    int insert(const core::Purchase& purchase);

    // Updates mutable fields: paid_cents, note, add_to_stock.
    // Does not allow changing supplier_id or total_cents after creation.
    bool updateMeta(int id, long long paidCents, const QString& note, bool addToStock);

    bool remove(int id);

private:
    Database& m_db;
};

} // namespace app::data