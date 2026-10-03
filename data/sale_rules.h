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

namespace detail {

// Whether a * b is representable as a long long. Signed overflow is undefined
// behaviour, and a wrapped amount is not a wrong amount, it is a plausible wrong
// amount of money.
//
// Done in unsigned magnitudes on purpose. The tempting shortcut
// `a < 0 && b < min / b` reads like a guard on the negative range and is not one:
// with a quantity of -2 and a cost of 5000, min / -2 comes out as a large
// positive number, so a comparison the wrong way round rejects every ordinary
// small figure and lets through the one that cannot be stored. A cancellation is
// exactly the case that needs this to be right — its lines are negative and
// perfectly ordinary.
inline bool productFits(long long a, long long b)
{
    // Two to the 63rd: the width of the negative range, one wider than max.
    const unsigned long long limit =
        static_cast<unsigned long long>(std::numeric_limits<long long>::max()) + 1ULL;
    const auto magnitude = [](long long value) -> unsigned long long {
        return value < 0 ? 0ULL - static_cast<unsigned long long>(value)
                          : static_cast<unsigned long long>(value);
    };
    const unsigned long long left = magnitude(a);
    const unsigned long long right = magnitude(b);
    if (left != 0 && right > limit / left) {
        return false;
    }
    const unsigned long long product = left * right;
    // The negative range reaches one further than the positive one, so the same
    // product can be too large to store either way round depending on the sign.
    return (a < 0) != (b < 0) ? product <= limit : product < limit;
}

// Whether a + b is representable, checked rather than performed.
inline bool sumFits(long long a, long long b)
{
    if (b > 0 && a > std::numeric_limits<long long>::max() - b) {
        return false;
    }
    return !(b < 0 && a < std::numeric_limits<long long>::min() - b);
}

} // namespace detail

// nullopt when the money does not fit. A sold-by-weight line is typed in
// kilograms, so quantity is a decimal the operator enters rather than a count
// of items, and a long enough line can push the multiplication past the range
// of the column it is about to be written to. Callers must refuse the sale
// rather than record a number.
inline std::optional<long long> totalCentsFor(const QVector<core::SaleItem>& items)
{
    long long total = 0;
    for (const core::SaleItem& item : items) {
        if (!detail::productFits(item.unitPriceCents, item.quantity)) {
            return std::nullopt;
        }
        const long long term = item.unitPriceCents * item.quantity;
        // Each term being in range is not enough: the running sum can leave it on
        // its own once enough lines are added together.
        if (!detail::sumFits(total, term)) {
            return std::nullopt;
        }
        total += term;
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
    long long cogs = 0;
    for (const core::SaleItem& item : items) {
        if (!detail::productFits(item.unitCostCents, item.quantity)) {
            return std::nullopt;
        }
        const long long term = item.unitCostCents * item.quantity;
        if (!detail::sumFits(cogs, term)) {
            return std::nullopt;
        }
        cogs += term;
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