#include "update_installer.h"

#include <QDir>
#include <QFileInfo>

namespace app::core {

namespace {

// cmd.exe expands %VAR% before it runs anything, so a path containing a
// percent sign would be mangled. A quote cannot survive at all, and a newline
// would end the command early. None of these are ordinary in a path, and all of
// them would produce a script that does something other than what it says.
bool isEmbeddable(const QString& path)
{
    return !path.contains(QLatin1Char('"')) && !path.contains(QLatin1Char('%'))
        && !path.contains(QLatin1Char('\n')) && !path.contains(QLatin1Char('\r'));
}

} // namespace

QString buildWindowsInstallBatch(const QString& appDir,
                                 const QString& archivePath,
                                 const QString& exeName)
{
    // Normalised to backslashes: this script is read by cmd and xcopy, and
    // mixing Qt's forward slashes with the joins below would be needlessly hard
    // to read in the one place a user might have to inspect it by hand.
    const QString app = QDir::toNativeSeparators(appDir);
    const QString archive = QDir::toNativeSeparators(archivePath);
    const QString exe = QDir::toNativeSeparators(exeName);

    if (!isEmbeddable(app) || !isEmbeddable(archive) || !isEmbeddable(exe) || exe.isEmpty()) {
        return QString();
    }

    // The archive's own directory is the working area, so the two cannot drift
    // apart the way %TEMP% on one side and QDir::tempPath() on the other would.
    const QString work = QFileInfo(archive).absolutePath() + QStringLiteral("\\Mahali-update");
    const QString errorFile =
        QFileInfo(archive).absolutePath() + QStringLiteral("\\mahali-update-error.txt");

    return QStringLiteral(R"BAT(@echo off
timeout /t 3 /nobreak >nul
taskkill /F /IM <EXE> >nul 2>&1
timeout /t 3 /nobreak >nul
taskkill /F /IM <EXE> >nul 2>&1
timeout /t 2 /nobreak >nul
powershell -NoProfile -Command "Expand-Archive -Path '<ARCHIVE>' -DestinationPath '<WORK>' -Force"
if not exist "<WORK>\<EXE>" (
    echo Échec de l'extraction. > "<ERROR>"
    start "" notepad.exe "<ERROR>"
    exit /b 1
)
robocopy "<WORK>" "<APP>" /E /R:5 /W:2 /NFL /NDL /NJH /NJS >nul
if %ERRORLEVEL% GEQ 8 (
    echo Échec de la copie. Code: %ERRORLEVEL% > "<ERROR>"
    start "" notepad.exe "<ERROR>"
    exit /b 1
)
if not exist "<APP>\<EXE>" (
    echo Fichier principal manquant après copie. > "<ERROR>"
    start "" notepad.exe "<ERROR>"
    exit /b 1
)
del "<ARCHIVE>"
rmdir /S /Q "<WORK>"
start "" "<APP>\<EXE>"
(goto) 2>nul & del "%~f0"
)BAT")
        .replace(QStringLiteral("<ARCHIVE>"), archive)
        .replace(QStringLiteral("<WORK>"), work)
        .replace(QStringLiteral("<ERROR>"), errorFile)
        .replace(QStringLiteral("<APP>"), app)
        .replace(QStringLiteral("<EXE>"), exe);
}

} // namespace app::core
