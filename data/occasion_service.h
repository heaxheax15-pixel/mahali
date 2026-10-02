#pragma once

#include <optional>

#include "core/occasion.h"
#include "audit_log_repository.h"
#include "database.h"
#include "occasion_repository.h"
#include "setting_repository.h"

namespace app::data {

// Which occasion is running. The choice is kept in settings rather than as a
// column on the occasion, because it is a state of the shop right now, not a
// property of the occasion itself.
class OccasionService {
public:
    OccasionService(Database& db,
                    OccasionRepository& occasions,
                    SettingRepository& settings);

    // The occasion running now, if any. A setting that points at a deleted or
    // deactivated occasion reads as none rather than as a broken reference.
    std::optional<core::Occasion> current() const;

    // Runs an occasion from now, writing its id into settings. The occasion has
    // to exist and be active: activating a disabled one would stamp sales with an
    // event the shop has turned off.
    // Runs inside a transaction so the setting and the audit log commit together.
    // Returns false if the occasion does not exist, is disabled, or the audit
    // log write fails.
    bool activate(int occasionId);

    // Stops whatever is running, by dropping the setting. Runs inside a
    // transaction so the setting and the audit log commit together. Returns false
    // if the audit log write fails.
    bool deactivate();

    // Whether the given moment falls inside the occasion's window. The bounds are
    // inclusive at both ends, so an occasion that starts and ends at the same
    // moment is open for that moment.
    bool isWithinRange(const core::Occasion& o, const QString& nowIso) const;

private:
    Database& m_db;
    OccasionRepository& m_occasions;
    SettingRepository& m_settings;
    AuditLogRepository m_audit;
};

} // namespace app::data
