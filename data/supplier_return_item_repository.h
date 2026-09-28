#pragma once

#include <vector>

#include "core/supplier_return_item.h"
#include "database.h"

namespace app::data {

class SupplierReturnItemRepository {
public:
    explicit SupplierReturnItemRepository(Database& db);

    std::vector<core::SupplierReturnItem> findByReturn(int returnId) const;
    int insert(const core::SupplierReturnItem& item);
    bool removeByReturn(int returnId);

private:
    Database& m_db;
};

} // namespace app::data
