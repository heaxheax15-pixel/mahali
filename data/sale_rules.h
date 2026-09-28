#pragma once

#include <limits>
#include <optional>
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

// nullopt when the money does not fit. A sold-by-weight line is typed in
// kilograms, so quantity is a decimal the operator enters rather than a count
// of items, and a long enough line can push the multiplication past the range
// of the column it is about to be written to. Signed overflow is undefined
// behaviour, and a wrapped total is not a wrong total, it is a plausible wrong
// amount of money. Callers must refuse the sale rather than record a number.
inline std::optional<long long> totalCentsFor(const QVector<core::SaleItem>& items)
{
    const long long ceiling = std::numeric_limits<long long>::max();
    const long long floor = std::numeric_limits<long long>::min();
    long long total = 0;
    for (const core::SaleItem& item : items) {
        const long long price = item.unitPriceCents;
        const long long quantity = item.quantity;
        if (quantity != 0) {
            if (quantity > 0 && price > ceiling / quantity) {
                return std::nullopt;
            }
            if (quantity < 0 && price < floor / quantity) {
                return std::nullopt;
            }
            // Each term being in range is not enough: the running sum can leave
            // it on its own once enough lines are added together.
            const long long term = price * quantity;
            if ((quantity > 0 && total > ceiling - term) || (quantity < 0 && total < floor - term)) {
                return std::nullopt;
            }
        }
        total += price * quantity;
    }
    return total;
}

// Same guard as totalCentsFor, and for the same reason: cost is multiplied by
// quantity exactly as price is, so a line long enough to overflow the price
// overflows the cost too. Reported apart from the total because cost is what
// the profit and loss statement is built from, and a wrapped figure there
// turns a loss into a margin.
inline std::optional<long long> cogsCentsFor(const QVector<core::SaleItem>& items)
{
    const long long ceiling = std::numeric_limits<long long>::max();
    const long long floor = std::numeric_limits<long long>::min();
    long long cogs = 0;
    for (const core::SaleItem& item : items) {
        const long long cost = item.unitCostCents;
        const long long quantity = item.quantity;
        if (quantity != 0) {
            if (quantity > 0 && cost > ceiling / quantity) {
                return std::nullopt;
            }
            if (quantity < 0 && cost < floor / quantity) {
                return std::nullopt;
            }
            const long long term = cost * quantity;
            if ((quantity > 0 && cogs > ceiling - term) || (quantity < 0 && cogs < floor - term)) {
                return std::nullopt;
            }
        }
        cogs += cost * quantity;
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