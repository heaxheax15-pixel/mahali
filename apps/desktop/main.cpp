#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QStandardPaths>
#include <QtGlobal>

#include "core/i18n.h"
#include "core/session.h"
#include "data/database.h"
#include "data/setting_repository.h"
#include "data/user_repository.h"
#include "core/format_utils.h"
#include "ui/login_dialog.h"
#include "ui/main_window.h"
#include "ui/server_controller.h"
#include "ui/theme.h"

int main(int argc, char* argv[])
{
    // app.qrc is compiled into the static libmahali-ui.a, so the linker drops
    // qrc_app.o unless the resource is initialised explicitly. Without this,
    // :/mahali/... never resolves and applyTheme() clears the stylesheet.
    Q_INIT_RESOURCE(app);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mahali"));
    app.setApplicationDisplayName(QCoreApplication::translate("main", "محلي"));
    app.setOrganizationName(QStringLiteral("mahali"));
    app.setApplicationVersion(QStringLiteral(MAHALI_VERSION));

    // The taskbar and window icon come from mahali.rc on Windows; this is the
    // in-app icon for every other platform and for the window's own icon.
    // Set here, before anything is built and long before any widget is shown,
    // because the login gate is the first thing on screen and it draws its own
    // title bar from this. A window that appears before the icon is known gets
    // the platform's placeholder painted first, which on Windows is the black
    // square the icon is meant to be sitting in.
    app.setWindowIcon(QIcon(QStringLiteral(":/mahali/icons/app.ico")));

    // Nothing here is a shortcut for the black flash: it is set so the gate's
    // own title bar and taskbar entry carry the mark from its first paint. The
    // flash has a different cause and is not addressed by this attribute.
    app.setAttribute(Qt::AA_DontShowIconsInMenus, true);

    QString dbPath;
    if (argc > 1) {
        dbPath = QString::fromLocal8Bit(argv[1]);
    } else {
        // A real desktop install keeps its data in the user's app-data folder.
        const QString dataDir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        dbPath = QDir(dataDir).filePath(QStringLiteral("mahali.sqlite"));
    }

    std::unique_ptr<app::data::Database> db;
    try {
        db = std::make_unique<app::data::Database>(dbPath, app::data::DatabaseMode::Server);
    } catch (const std::exception& e) {
        QMessageBox::critical(
            nullptr, QCoreApplication::translate("main", "محلي — خطأ"),
            QCoreApplication::translate("main", "تعذر فتح قاعدة البيانات:\n%1")
                .arg(QString::fromUtf8(e.what())));
        return 1;
    }

    // The sync key is set once in Settings (Phase 14); default for first launch.
    app::data::SettingRepository settings(*db);
    const QByteArray hmacKey = settings.value(QStringLiteral("sync_hmac_key"))
                                   .value_or(QStringLiteral("mahali-local-key"))
                                   .toUtf8();

    // Language: persist the default on first launch, then load the catalogue and
    // pin the layout direction before any window is built.
    if (!settings.value(QStringLiteral("language")).has_value()) {
        settings.set(QStringLiteral("language"), app::core::defaultLanguage());
    }
    app::core::applyLanguage(app::core::currentLanguage(*db));

    app::ui::setCurrencySymbol(settings.value(QStringLiteral("currency_symbol")).value_or(QString()));

    // Apply the stored theme before the window appears (default: light).
    app::ui::applyTheme(settings.value(QStringLiteral("theme")).value_or(QStringLiteral("light")), app);

    // Login flow. This is the first window the process shows, and it is shown before
    // the server is started and before the shell is built: the gate needs nothing
    // but the database, and starting the sync server or laying out fourteen pages
    // behind an open dialog is work that would delay the one thing the shop is
    // waiting for. Nothing here runs processEvents either, so the dialog is fully
    // built and styled by the time exec() puts it on screen -- a window that
    // repaints itself in stages while the rest of the start-up runs behind it is
    // what a flash of unstyled black looks like.
    app::data::UserRepository userRepo(*db);
    app::ui::LoginDialog login(*db);
    if (login.exec() != QDialog::Accepted) {
        return 0;
    }

    app::ui::ServerController controller(*db, hmacKey);
    controller.start();

    app::ui::MainWindow window(*db, controller);
    // Maximized rather than shown at its own size. The shell is a full till: the
    // page, the table and the top bar are all laid out to use the width they are
    // given, and a window the operator then has to maximise by hand would show
    // them all at their minimum sizes first. showMaximized() also happens before
    // the first paint, so the window is never seen at the smaller size and grown.
    window.showMaximized();
    return app.exec();
}