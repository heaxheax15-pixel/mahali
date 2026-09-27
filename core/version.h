#pragma once

#include <QString>

namespace app::core {

// A release version, compared component by component. Kept as plain ints so the
// update checker never has to parse strings to decide whether to offer an
// upgrade.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// The version baked in at build time from the project version in CMakeLists.
Version currentVersion();

// True when `a` is strictly newer than `b` (1.0.1 beats 1.0.0, 2.0.0 beats
// 1.9.9).
bool isNewer(const Version& a, const Version& b);

// Parses a release tag: a leading "v" is optional, and a missing patch defaults
// to 0, so "v1.0.1" -> {1,0,1} and "1.5" -> {1,5,0}. Anything that does not
// start with a number parses as {0,0,0}, which is older than every real release
// and therefore never offered as an upgrade.
Version parseVersion(const QString& tag);

} // namespace app::core
