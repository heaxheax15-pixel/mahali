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

    int save(const core::Product& product);
    void adjustStock(int productId, long long delta, const QString& reason);
    void setActive(int productId, bool active);

private:
    Database& m_db;
};

} // namespace app::data