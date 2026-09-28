#include "occasion_service.h"

#include "date_utils.h"

namespace app::data {

namespace {
// The one setting that says which occasion is running. Kept here so the key is
// written in one place: a second spelling of it would leave an occasion running
// that nothing can turn off.
const char* kActiveOccasionKey = "active_occasion_id";
} // namespace

OccasionService::OccasionService(Database& db,
                                 OccasionRepository& occasions,
                                 SettingRepository& settings)
    : m_db(db)
    , m_occasions(occasions)
    , m_settings(settings)
{
}

std::optional<core::Occasion> OccasionService::current() const
{
    const std::optional<QString> stored =
        m_settings.value(QString::fromLatin1(kActiveOccasionKey));
    if (!stored.has_value()) {
        return std::nullopt;
    }

    // A setting that is not a number, or points at nothing, is treated as no
    // occasion rather than trusted: a stale key must not put a half-readable
    // occasion on the status bar.
    bool ok = false;
    const int id = stored->toInt(&ok);
    if (!ok || id <= 0) {
        return std::nullopt;
    }

    const std::optional<core::Occasion> occasion = m_occasions.findById(id);
    if (!occasion.has_value() || !occasion->active) {
        return std::nullopt;
    }
    return occasion;
}

bool OccasionService::activate(int occasionId)
{
    const std::optional<core::Occasion> occasion = m_occasions.findById(occasionId);
    if (!occasion.has_value()) {
        return false;
    }
    // Stamping sales with a disabled occasion would attribute a day's takings to
    // an event the shop has turned off, so the switch is checked here.
    if (!occasion->active) {
        return false;
    }

    m_settings.set(QString::fromLatin1(kActiveOccasionKey), QString::number(occasionId));
    // set() reports through the database rather than a return value, so the row is
    // read back to be sure the occasion really is the one that will be reported.
    const std::optional<QString> stored =
        m_settings.value(QString::fromLatin1(kActiveOccasionKey));
    return stored.has_value() && stored->toInt() == occasionId;
}

void OccasionService::deactivate()
{
    m_settings.remove(QString::fromLatin1(kActiveOccasionKey));
}

bool OccasionService::isWithinRange(const core::Occasion& o, const QString& nowIso) const
{
    // Bounds and the moment are all stored as ISO text. Parsing them keeps the
    // comparison on instants rather than on characters, so an occasion recorded
    // in a different zone or precision is still judged by when it happens. A
    // bound that will not parse leaves the occasion with no usable window, and
    // an unparseable moment cannot be placed inside one.
    const std::optional<QDateTime> start = fromIso(o.startsAt);
    const std::optional<QDateTime> end = fromIso(o.endsAt);
    const std::optional<QDateTime> now = fromIso(nowIso);
    if (!start.has_value() || !end.has_value() || !now.has_value()) {
        return false;
    }

    // Inclusive at both ends, so an occasion that opens and closes at the same
    // moment covers that moment.
    if (*now < *start) {
        return false;
    }
    if (*now > *end) {
        return false;
    }
    return true;
}

} // namespace app::data
