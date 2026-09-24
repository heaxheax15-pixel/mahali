#pragma once

#include <QDateTime>
#include <optional>
#include <vector>

#include "core/audit_log_entry.h"
#include "database.h"

namespace app::data {

class AuditLogRepository {
public:
    explicit AuditLogRepository(Database& db);

    std::optional<core::AuditLogEntry> findById(int id) const;
    std::vector<core::AuditLogEntry> findBetween(const QDateTime& from, const QDateTime& to) const;
    std::vector<core::AuditLogEntry> findAll() const;

    int insert(const core::AuditLogEntry& entry);

private:
    Database& m_db;
};

} // namespace app::data