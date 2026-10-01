#pragma once

#include <QString>
#include <optional>
#include <vector>

#include "core/product.h"
#include "database.h"

namespace app::data {

class ProductRepository {
public:
    // How findAll() treats products that have no real name -- a name holding no
    // Unicode letter at all. An import from another till writes rows like "12345"
    // or "---" where a name should be, and those are not products anyone browses
    // for. They stay in the table and stay sellable by scanning, which is why
    // findByBarcode() below still sees them; they are only kept out of a list.
    enum class Visibility {
        Visible,  // only products whose name carries a letter
        All,      // every row, named or not
    };

    explicit ProductRepository(Database& db);

    std::optional<core::Product> findById(int id) const;
    std::optional<core::Product> findByBarcode(const QString& barcode) const;

    // Defaults to Visibility::Visible, so a list built from this repository does
    // not show the nameless rows. Ask for Visibility::All when the caller needs
    // to see them, such as an export or a repair tool.
    std::vector<core::Product> findAll(Visibility visibility = Visibility::Visible) const;

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