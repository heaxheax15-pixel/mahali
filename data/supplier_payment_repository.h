#pragma once

#include <QDateTime>
#include <optional>
#include <vector>

#include "core/supplier_payment.h"
#include "database.h"

namespace app::data {

class SupplierPaymentRepository {
public:
    explicit SupplierPaymentRepository(Database& db);

    std::optional<core::SupplierPayment> findById(int id) const;
    std::vector<core::SupplierPayment> findBySupplierId(int supplierId) const;
    std::vector<core::SupplierPayment> findByPurchaseId(int purchaseId) const;
    std::vector<core::SupplierPayment> findBetween(const QDateTime& from, const QDateTime& to) const;

    int insert(const core::SupplierPayment& payment);

private:
    Database& m_db;
};

} // namespace app::data
