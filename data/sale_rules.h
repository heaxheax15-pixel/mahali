#pragma once

#include <limits>
#include <optional>
#include <QString>
#include <QVector>

#include "product_repository.h"
#include "stock_movement_repository.h"

namespace app::data {

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
        // The unit and the piece count are settled before the price below, because
        // the price of a carton is derived from the same count the pieces are
        // counted in. Filling piecesConsumed here rather than at the repository
        // insert is what makes the money path (cogsCentsFor, insertSaleStockMovements)
        // and the stored row agree by construction, which is the entire reason
        // resolveSaleItems exists. Setting it at the insert site is too late: both
        // of those run on this resolved vector before the row is ever built.

        // An empty unitKind can only come from a caller that predates this field;
        // treating it as a piece line is what was true of every line before
        // Phase 2B, and refusing it would break every existing path that has not
        // yet been taught to set it.
        if (item.unitKind.isEmpty()) {
            item.unitKind = QStringLiteral("piece");
        }

        if (item.unitKind == QStringLiteral("package")) {
            // The carton price is the piece price times the count, and the caller
            // is allowed to know it already. When it does not, we compute it here
            // so a package line is always priced. Only ever the price of ONE carton:
            // totalCentsFor multiplies by quantity afterwards, so multiplying here
            // too would charge the customer for twice what they took.
            if (item.unitPriceCents <= 0) {
                // Same overflow shape as the COGS multiplication, so the same guard.
                if (!detail::productFits(product->salePriceCents,
                                         product->piecesPerPackage)) {
                    if (error) {
                        *error = QStringLiteral("package price overflows for product %1")
                                     .arg(product->name);
                    }
                    return {};
                }
                item.unitPriceCents = product->salePriceCents * product->piecesPerPackage;
            }
            // How many pieces leave the shelf. Guarded for the same reason and with
            // the same check: a carton of a million pieces would otherwise wrap into
            // a small positive number and take that many pieces off the shelf.
            if (!detail::productFits(item.quantity, product->piecesPerPackage)) {
                if (error) {
                    *error = QStringLiteral("package piece count overflows for product %1")
                                 .arg(product->name);
                }
                return {};
            }
            item.piecesConsumed = item.quantity * product->piecesPerPackage;
        } else {
            // sold-by-weight is not a carton; its quantity is already the figure that
            // leaves the shelf, and a piece line's is too. The caller's string is kept
            // as it was typed rather than folded into 'piece', because 'kg' says
            // something a reader of the ledger needs to know.
            item.piecesConsumed = item.quantity;
        }

        if (item.unitPriceCents <= 0) {
            item.unitPriceCents = product->salePriceCents;
        }
        item.unitCostCents = product->costPriceCents;

        // the shelf is counted in pieces, so the guard must be too.
        // Comparing pieces against cartons lets a carton sale of 2 pass when 30
        // pieces are on the shelf.
        if (!allowOversold && product->quantity < item.piecesConsumed) {
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
// of the column it is about to be written to. Callers must refuse the sale
// rather than record a number.
//
// quantity is CORRECT here, and this is the only money function where it is. The
// price on a line is quoted per unit sold, not per piece: a carton of 24 sold at
// 1450 is 1450 of revenue, not 1450 x 24, because 1450 is what the customer was
// charged for the carton and that is the whole of what came in. Multiplying by
// pieces_consumed instead would bill the customer 24 times over for one line.
//
// The split with cogsCentsFor below is the reason both numbers are kept on the
// row. Revenue is about what the unit sold for and is counted in units; cost is
// about what the goods cost and is counted in pieces, because the piece is what
// the shop bought. The two use different units because they are answers to
// different questions, so a line that carried only one count could not answer both.
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

// The cost of the goods, counted in PIECES rather than in units sold. This is the
// one money figure that must not use quantity: a unit price is quoted per piece and
// a carton price per carton, but costPriceCents is always what one piece cost the
// shop. Multiplying it by quantity would under-count the cost of a carton sale by a
// factor of pieces_per_package -- 24 pieces of goods gone for the price of one --
// and a sale that under-counts its own cost reports a profit the shop did not make.
// The profit and loss statement is built from this figure, so the error would not
// stay on one row: it would walk straight into the day's margin.
//
// Same guard as totalCentsFor, and for the same reason: cost is multiplied by a
// count exactly as price is, so a line long enough to overflow the price overflows
// the cost too. Reported apart from the total because cost is what the profit and
// loss statement is built from, and a wrapped figure there turns a loss into a
// margin. The operands change; the checks do not.
inline std::optional<long long> cogsCentsFor(const QVector<core::SaleItem>& items)
{
    long long cogs = 0;
    for (const core::SaleItem& item : items) {
        if (!detail::productFits(item.unitCostCents, item.piecesConsumed)) {
            return std::nullopt;
        }
        const long long term = item.unitCostCents * item.piecesConsumed;
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
        // Stock is counted in pieces, so it takes piecesConsumed and not quantity.
        // The two agree on a piece sale and differ by a factor of pieces_per_package
        // on a carton sale, and stock is the figure that has to be right: counting a
        // carton off the shelf as one piece would leave 23 pieces on the shelf that
        // are not there, and the daily stock count would then disagree with the till.
        movement.delta = -item.piecesConsumed;
        movement.reason = QStringLiteral("sale");
        movement.createdAt = QDateTime::currentDateTime();
        if (movements.insert(movement) == 0) {
            return false;
        }
    }
    return true;
}

} // namespace app::data