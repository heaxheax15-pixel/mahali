#include "core/zakat_notifier.h"

#include <QDate>
#include <QString>

#include "data/setting_repository.h"

namespace app::core {

namespace {

// How long after the date the reminder keeps asking. Long enough to cover a shop
// that is only opened some days of the week, short enough that a date set for
// last month is not still being announced this month.
constexpr int kReminderWindowDays = 7;

} // namespace

int zakatYearFor(const QDate& today, const QDate& zakatDate)
{
    if (!today.isValid()) {
        return 0;
    }
    if (!zakatDate.isValid()) {
        return today.year();
    }
    QDate occurrence(today.year(), zakatDate.month(), zakatDate.day());
    if (!occurrence.isValid()) {
        // 29 February in a year that is not a leap year has no such day, so the
        // turn-over lands on the 28th. Only reachable for a 29-02 zakat_date, and
        // landing a day early is the better of the two wrong answers: the year
        // ends on time rather than one day late.
        occurrence = QDate(today.year(), zakatDate.month(), 28);
    }
    return today >= occurrence ? today.year() : today.year() - 1;
}

bool shouldNotifyZakat(app::data::Database& db)
{
    data::SettingRepository settings(db);

    const QString stored =
        settings.value(QStringLiteral("zakat_date")).value_or(QString()).trimmed();
    if (stored.isEmpty()) {
        return false;
    }
    const QDate zakatDate = QDate::fromString(stored, QStringLiteral("yyyy-MM-dd"));
    if (!zakatDate.isValid()) {
        return false;
    }

    // A pure read. It answers "is this year's zakat still outstanding and is this
    // the moment to ask?" and writes nothing, so asking the question cannot be
    // what silences it. Claiming the year is markZakatYearAsSeen's job, called by
    // whoever actually settles it — a dialog that merely declined used to burn the
    // claim here and the shop was never asked again.
    //
    // The window is measured against this year's occurrence of the saved day,
    // not against the bare month and day. A 01-04 date reached in 2026 opens the
    // window on 2026-04-01; in 2027 it opens on 2027-04-01. Comparing against the
    // bare date instead would silently stop the reminder after the first year.
    const QDate today = QDate::currentDate();
    const int currentYear = zakatYearFor(today, zakatDate);
    QDate occurrence(today.year(), zakatDate.month(), zakatDate.day());
    if (!occurrence.isValid()) {
        occurrence = QDate(today.year(), zakatDate.month(), 28);
    }

    const QString notified =
        settings.value(QStringLiteral("zakat_notified_year")).value_or(QString()).trimmed();
    if (notified == QString::number(currentYear)) {
        return false;
    }

    // Outside the window this is not the moment.
    if (today < occurrence || today > occurrence.addDays(kReminderWindowDays)) {
        return false;
    }

    return true;
}

void markZakatYearAsSeen(app::data::Database& db, int year)
{
    data::SettingRepository(db).set(QStringLiteral("zakat_notified_year"), QString::number(year));
}

} // namespace app::core