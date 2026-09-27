#pragma once

#include <QString>

namespace app::core {

// Builds the batch script that unpacks a downloaded archive over a running
// installation, once that installation has quit.
//
// This is a pure string builder with no Qt networking, no widgets and no
// Windows-only headers, so it compiles and is tested on every platform — the
// platform decision (whether to run it at all) belongs to the caller.
//
// Every path is written out in full rather than left to %TEMP%: the downloader
// decides where the archive lives, and the script reads its paths from that
// same answer instead of re-deriving them from the environment, where a
// divergence would silently unpack nothing.
//
// Returns an empty string when any path cannot be embedded safely. A script
// that quotes a path badly is worse than no script: it would copy files to the
// wrong place, or run a command that was not the one intended.
QString buildWindowsInstallBatch(const QString& appDir,
                                 const QString& archivePath,
                                 const QString& exeName);

} // namespace app::core
