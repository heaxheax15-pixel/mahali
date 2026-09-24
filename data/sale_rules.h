#pragma once

#include <QString>
#include <QVector>

#include "product_repository.h"
#include "stock_movement_repository.h"

namespace app::data {

// Shared accounting rules for recording a sale or customer debt. Both the
// Windows-side SaleService (server apply) and the device-side DeviceLedgerService
// (offline record) funnel through these same functions so money math never
// diverges between the two machines.

inline QVector<core::SaleItem> resolveSaleItems(ProductRepository& products,
                                                const QVector<core::SaleItem>& items, bool allowOversold,
                                                QString* error)
{
    QVector<core::SaleItem> resolved = items;
    for (core::SaleItem& item : resolved) {
        const auto product = products.findById(item.productId);
        if (!product.has_value()) {
            if (error) {
                *error = QStringLiteral("unknown product id %1").arg(item.productId);
            }
            return {};
        }
        if (item.unitPriceCents <= 0) {
            item.unitPriceCents = product->salePriceCents;
        }
        item.unitCostCents = product->costPriceCents;
        if (!allowOversold && product->quantity < item.quantity) {
            if (error) {
                *error = QStringLiteral("insufficient stock for product %1").arg(product->name);
            }
            return {};
        }
    }
    return resolved;
}

inline long long totalCentsFor(const QVector<core::SaleItem>& items)
{
    long long total = 0;
    for (const core::SaleItem& item : items) {
        total += item.unitPriceCents * item.quantity;
    }
    return total;
}

inline long long cogsCentsFor(const QVector<core::SaleItem>& items)
{
    long long cogs = 0;
    for (const core::SaleItem& item : items) {
        cogs += item.unitCostCents * item.quantity;
    }
    return cogs;
}

inline bool insertSaleStockMovements(StockMovementRepository& movements,
                                     const QVector<core::SaleItem>& items)
{
    for (const core::SaleItem& item : items) {
        core::StockMovement movement;
        movement.productId = item.productId;
        movement.delta = -item.quantity;
        movement.reason = QStringLiteral("sale");
        movement.createdAt = QDateTime::currentDateTime();
        if (movements.insert(movement) == 0) {
            return false;
        }
    }
    return true;
}

} // namespace app::data