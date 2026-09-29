#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QUrl>
#include <QtTest/QtTest>

#include "core/update_checker.h"
#include "core/update_downloader.h"
#include "core/update_installer.h"
#include "core/version.h"

using namespace app;

// Phase: the update-notification data layer. Only the pure version logic is
// covered here; UpdateChecker's GitHub round-trip needs a network and stays out
// of the suite, but the URL and the tag comparison it relies on are checked.
class UpdateTest : public QObject
{
    Q_OBJECT

private slots:
    void version_parse();
    void version_compare();
    void current_version_matches_macro();
    void release_url_is_the_one_the_installer_publishes_to();
    void downloader_urls_the_published_installer();
    void downloader_start_with_bad_host_fails();
    void update_script_contains_checks();
    void update_script_refuses_unsafe_paths();
};

void UpdateTest::downloader_urls_the_published_installer()
{
    // The asset the Windows workflow uploads, reached through the "latest"
    // alias so a new release needs no change here.
    QCOMPARE(app::core::UpdateDownloader::defaultUrl().toString(),
             QStringLiteral("https://github.com/heaxheax15-pixel/mahali/releases/"
                            "latest/download/Mahali-Setup.zip"));

    // The batch script the restart step runs unpacks exactly this name, so the
    // two must not drift apart silently.
    QVERIFY(app::core::UpdateDownloader::defaultDestination()
                .endsWith(QStringLiteral("Mahali-Setup.zip")));
    QVERIFY(app::core::UpdateDownloader::defaultDestination().startsWith(QDir::tempPath()));
}

void UpdateTest::downloader_start_with_bad_host_fails()
{
    app::core::UpdateDownloader downloader;

    // Port 1 on loopback: nothing is listening, so the connection is refused
    // immediately. No network and no real GitHub call, and still the same code
    // path a shop with no internet would walk.
    downloader.setUrl(QUrl(QStringLiteral("http://127.0.0.1:1/Mahali-Setup.zip")));
    const QString destination =
        QDir(QDir::tempPath()).filePath(QStringLiteral("tst-update-download.zip"));
    downloader.setDestination(destination);

    QSignalSpy progress(&downloader, &app::core::UpdateDownloader::progress);
    QSignalSpy finishedSpy(&downloader, &app::core::UpdateDownloader::finished);
    QSignalSpy failedSpy(&downloader, &app::core::UpdateDownloader::failed);

    QElapsedTimer timer;
    timer.start();
    downloader.start();

    QVERIFY2(failedSpy.wait(3000), "an unreachable host must fail, not hang");
    QVERIFY2(timer.elapsed() < 3000, "the failure took too long to surface");

    // A refused connection is not a download: nothing may be reported as
    // finished, and no half-written file may be left for the restart step to
    // find and unpack.
    QVERIFY(finishedSpy.isEmpty());
    // QNetworkReply reports one progress(0) as the request opens, before the
    // connection is refused. What must never appear is a real percentage: a
    // refused connection transferred nothing.
    for (const QList<QVariant>& reported : progress) {
        QCOMPARE(reported.at(0).toInt(), 0);
    }
    QVERIFY2(!QFile::exists(destination), "a failed download left a file behind");
}

void UpdateTest::update_script_contains_checks()
{
    const QString appDir = QStringLiteral("C:/Mahali");
    const QString archive = app::core::UpdateDownloader::defaultDestination();
    const QString script = app::core::buildWindowsInstallBatch(
        appDir, archive, QStringLiteral("mahali-desktop.exe"));

    QVERIFY(!script.isEmpty());

    // Both failure paths stop the script. Without them a corrupt archive left
    // the script copying nothing and restarting the app, so the user saw a
    // restart and no update.
    QVERIFY2(script.contains(QStringLiteral("if not exist")),
             "a failed extraction is not detected");
    QVERIFY2(script.contains(QStringLiteral("if errorlevel 1")),
             "a failed copy is not detected");
    QVERIFY(script.contains(QStringLiteral("exit /b 1")));

    // Paths come from the downloader, not from the environment. %TEMP% on one
    // side and QDir::tempPath() on the other is how a script ends up unpacking
    // a file that was never downloaded.
    QVERIFY2(!script.contains(QStringLiteral("%TEMP%")),
             "the script still leaves a path to the environment");
    QVERIFY2(script.contains(QDir::toNativeSeparators(QDir::tempPath())),
             "the script does not point at the temp directory the app uses");

    // The archive path must be the one the downloader writes, still quoted.
    const QString nativeArchive = QDir::toNativeSeparators(archive);
    QVERIFY2(script.contains(QStringLiteral("\"") + nativeArchive + QStringLiteral("\"")),
             "the archive path is missing or unquoted in the script");

    // The executable the script launches is the one it also checks for, so the
    // extraction test and the restart cannot disagree.
    QVERIFY(script.contains(QStringLiteral("\"") + QDir::toNativeSeparators(appDir)
                            + QStringLiteral("\\mahali-desktop.exe\"")));
}

void UpdateTest::update_script_refuses_unsafe_paths()
{
    const QString archive = app::core::UpdateDownloader::defaultDestination();

    // A quote or a percent sign would escape the quoting or be expanded as a
    // variable, so no script is produced at all. An empty result is the caller's
    // signal to tell the user rather than to run something unpredictable.
    QVERIFY(app::core::buildWindowsInstallBatch(
                QStringLiteral("C:/Mahali \"evil\""), archive,
                QStringLiteral("mahali-desktop.exe"))
                .isEmpty());
    QVERIFY(app::core::buildWindowsInstallBatch(
                QStringLiteral("C:/100% Mahali"), archive,
                QStringLiteral("mahali-desktop.exe"))
                .isEmpty());
    // An empty executable name would produce a script that checks for a
    // directory it cannot name, and then starts nothing.
    QVERIFY(app::core::buildWindowsInstallBatch(QStringLiteral("C:/Mahali"), archive,
                                                 QString())
                .isEmpty());
}

void UpdateTest::version_parse()
{
    const app::core::Version full = app::core::parseVersion(QStringLiteral("v1.0.0"));
    QCOMPARE(full.major, 1);
    QCOMPARE(full.minor, 0);
    QCOMPARE(full.patch, 0);

    const app::core::Version another = app::core::parseVersion(QStringLiteral("v2.3.4"));
    QCOMPARE(another.major, 2);
    QCOMPARE(another.minor, 3);
    QCOMPARE(another.patch, 4);

    // A missing patch component defaults to 0, and the "v" stays optional.
    const app::core::Version shortTag = app::core::parseVersion(QStringLiteral("1.5"));
    QCOMPARE(shortTag.major, 1);
    QCOMPARE(shortTag.minor, 5);
    QCOMPARE(shortTag.patch, 0);

    // Extra components are dropped rather than folded into the patch.
    const app::core::Version longTag = app::core::parseVersion(QStringLiteral("v1.2.3.4"));
    QCOMPARE(longTag.patch, 3);

    // A pre-release suffix is ignored, not misread as a number.
    const app::core::Version rc = app::core::parseVersion(QStringLiteral("v1.2.3-rc1"));
    QCOMPARE(rc.major, 1);
    QCOMPARE(rc.minor, 2);
    QCOMPARE(rc.patch, 3);

    // Junk never parses as a version a user could be "offered" an upgrade to.
    QCOMPARE(app::core::parseVersion(QString()).major, 0);
    QCOMPARE(app::core::parseVersion(QStringLiteral("v")).major, 0);
    QCOMPARE(app::core::parseVersion(QStringLiteral("latest")).major, 0);
}

void UpdateTest::version_compare()
{
    QVERIFY(app::core::isNewer({1, 0, 1}, {1, 0, 0}));
    QVERIFY(app::core::isNewer({2, 0, 0}, {1, 9, 9}));
    QVERIFY(!app::core::isNewer({1, 0, 0}, {1, 0, 0}));
    QVERIFY(!app::core::isNewer({1, 0, 0}, {1, 0, 1}));

    // A patch bump is the difference between "up to date" and an update prompt,
    // so it has to be caught. The probe sits one patch above the build.
    QVERIFY(app::core::isNewer({1, 0, 2}, app::core::currentVersion()));
}

void UpdateTest::current_version_matches_macro()
{
    // Keeps the checker and the .rc in step: the build version is 1.0.1.
    const app::core::Version current = app::core::currentVersion();
    QCOMPARE(current.major, 1);
    QCOMPARE(current.minor, 0);
    QCOMPARE(current.patch, 1);
    QCOMPARE(QStringLiteral(MAHALI_VERSION), QStringLiteral("1.0.1"));
}

void UpdateTest::release_url_is_the_one_the_installer_publishes_to()
{
    QCOMPARE(app::core::UpdateChecker::latestReleaseUrl().toString(),
             QStringLiteral("https://api.github.com/repos/heaxheax15-pixel/mahali/releases/latest"));
}

QTEST_MAIN(UpdateTest)
#include "tst_update.moc"
