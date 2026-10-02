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

    // The one call a service makes: the actor and the moment are filled in from
    // the session and the clock, so no call site can leave either to chance.
    //
    // Written inside the service's own transaction, which is the whole point. An
    // audit row written after the commit says the money moved whether or not it
    // did, and one written before it is rolled back with the movement it was
    // describing — a log that records an expense that never happened is worse
    // than no log at all, because it is believed.
    //
    // Returns 0 on failure like insert(), and the callers treat that as fatal to
    // the whole operation: an unaudited movement is not one to keep.
    int record(const QString& action, const QString& target);
    int recordAs(const QString& actor, const QString& action, const QString& target);

private:
    Database& m_db;
};

} // namespace app::data