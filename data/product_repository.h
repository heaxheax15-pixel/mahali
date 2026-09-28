#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "core/product.h"
#include "database.h"

namespace app::data {

class ProductRepository {
public:
    explicit ProductRepository(Database& db);

    std::optional<core::Product> findById(int id) const;
    std::optional<core::Product> findByBarcode(const QString& barcode) const;
    std::vector<core::Product> findAll() const;

    // Quick items are active products with no barcode (NULL, or blank). They
    // are sold without scanning, so they are never looked up by barcode.
    std::vector<core::Product> findQuickItems() const;
    std::vector<core::Product> findQuickItemsByName(const QString& query) const;

    int save(const core::Product& product);
    void adjustStock(int productId, long long delta, const QString& reason);
    void setActive(int productId, bool active);
    void setSoldByWeight(int productId, bool value);

    // Moving average cost (PMP) after a purchase line:
    //   newCost = (oldQty * oldCost + addedQty * addedCost) / (oldQty + addedQty)
    // Whole cents only, no float. A stock level that would not move past zero
    // leaves the stored cost alone, since there is no ratio to weigh against.
    // Returns false only when the UPDATE itself failed.
    bool updateAverageCost(int productId, long long oldQty, long long oldCostCents, long long addedQty,
                           long long addedCostCents);

private:
    Database& m_db;
};

} // namespace app::data