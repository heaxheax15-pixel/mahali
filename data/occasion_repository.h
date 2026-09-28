#pragma once

#include <optional>
#include <QString>
#include <QVector>

#include "core/occasion.h"
#include "database.h"

namespace app::data {

class OccasionRepository {
public:
    explicit OccasionRepository(Database& db);

    std::optional<core::Occasion> findById(int id) const;
    QVector<core::Occasion> findAll() const;
    QVector<core::Occasion> findActive() const;
    int insert(const core::Occasion& o);
    bool update(const core::Occasion& o);
    bool remove(int id);
    bool setActive(int id, bool active);

private:
    Database& m_db;
};

} // namespace app::data
