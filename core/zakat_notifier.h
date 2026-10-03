#pragma once

#include <QDate>

namespace app::data {
class Database;
}

namespace app::core {

// Which zakat year `today` falls in, given the day of the year the operator saved
// in "zakat_date". Pure: it reads nothing and is told both dates, so the year
// reckoning can be checked without a database.
//
// The date is a day within a trading year, not a date on the calendar: 01-04 means
// "the first of April, whenever that falls". So the year turns over on that day,
// not on 1 January. Asked on 15 February 2026 with a 01-04 date, the answer is
// 2025 — the 2026 zakat year has not begun yet. Asked on 1 April 2026 it is 2026.
//
// An invalid `today` has no year to give, so the answer is 0. An invalid
// `zakatDate` cannot place the turn-over, so the answer is today's own year: the
// safest reading of a shop whose date was never set.
int zakatYearFor(const QDate& today, const QDate& zakatDate);

// Whether the annual zakat reminder is owed right now, as set by the operator in
// Settings ("zakat_date", a YYYY-MM-DD day of the trading year).
//
// True only inside a seven-day window that opens on that date, and only if that
// year has not been claimed in "zakat_notified_year". No date set, or a date that
// will not parse, means no reminder — the feature stays off until somebody asks
// for it.
//
// A pure read: it claims nothing. Asking whether the reminder is owed must not be
// what settles it, or a reminder dismissed without paying would be silently
// spent. Claim the year with markZakatYearAsSeen once it has actually been dealt
// with; until then the reminder is owed again on the next run, which is the
// point.
//
// Note: .cpp lives in the mahali-data target, because reading that key needs
// SettingRepository and data depends on core, not the reverse.
bool shouldNotifyZakat(app::data::Database& db);

// Records that the year has been dealt with, writing "zakat_notified_year".
// Separate from shouldNotifyZakat on purpose: only something that genuinely
// settled the year's zakat — a payment recorded — has any business silencing it,
// so this is called from where the payment is saved and nowhere else.
void markZakatYearAsSeen(app::data::Database& db, int year);

} // namespace app::core