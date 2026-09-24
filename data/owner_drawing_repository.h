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
    void reverse(int originalDrawingId);

private:
    Database& m_db;
};

} // namespace app::data