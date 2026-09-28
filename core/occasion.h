#pragma once

#include <QString>

namespace app::core {

// A named period the shop marks up, a holiday or a promotion. One of them can
// be running at a time, and sales and purchases made while it runs are stamped
// with it so the reports can be read per event.
struct Occasion {
    int id = 0;
    QString name;
    // The window the occasion covers, stored as text. An occasion is open
    // between startsAt and endsAt, and only one is ever running.
    QString startsAt;
    QString endsAt;
    // An optional emoji or short tag shown next to the name in the status bar.
    QString icon;
    // Whether the occasion may be activated. Kept apart from "is running" on
    // purpose: an occasion can be available all year and still be off today.
    bool active = true;
    QString createdAt;
};

} // namespace app::core
