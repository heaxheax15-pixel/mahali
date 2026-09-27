#pragma once

#include <QDateTime>
#include <optional>
#include <vector>

#include "core/owner_drawing.h"
#include "database.h"

namespace app::data {

class OwnerDrawingRepository {
public:
    explicit OwnerDrawingRepository(Database& db);

    std::optional<core::OwnerDrawing> findById(int id) const;
    std::vector<core::OwnerDrawing> findBetween(const QDateTime& from, const QDateTime& to) const;

    int insert(const core::OwnerDrawing& drawing);

    // Writes the mirrored row and reports whether it was written. Returns
    // false — changing nothing — when the original is gone, is itself a
    // reversal, or has already been reversed.
    //
    // No transaction is opened here: CashEntryService calls this inside one it
    // owns, because the mirrored row and the cash movement that returns the
    // money to the owner have to land together or not at all. A caller with no
    // transaction of its own gets an uncommitted row, same as insert().
    bool reverse(int originalDrawingId);

private:
    Database& m_db;
};

} // namespace app::data