#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QStandardPaths>

#include "core/i18n.h"
#include "core/session.h"
#include "data/database.h"
#include "data/setting_repository.h"
#include "data/user_repository.h"
#include "ui/format_utils.h"
#include "ui/login_dialog.h"
#include "ui/main_window.h"
#include "ui/server_controller.h"
#include "ui/theme.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mahali"));
    app.setApplicationDisplayName(QCoreApplication::translate("main", "محلي"));
    app.setOrganizationName(QStringLiteral("mahali"));
    app.setApplicationVersion(QStringLiteral(MAHALI_VERSION));

    QIcon appIcon;
    appIcon.addFile(QStringLiteral(":/mahali/icons/app-512.png"), QSize(512, 512));
    appIcon.addFile(QStringLiteral(":/mahali/icons/app-256.png"), QSize(256, 256));
    appIcon.addFile(QStringLiteral(":/mahali/icons/app-128.png"), QSize(128, 128));
    appIcon.addFile(QStringLiteral(":/mahali/icons/app-64.png"), QSize(64, 64));
    appIcon.addFile(QStringLiteral(":/mahali/icons/app-48.png"), QSize(48, 48));
    appIcon.addFile(QStringLiteral(":/mahali/icons/app-32.png"), QSize(32, 32));
    app.setWindowIcon(appIcon);

    QFile style(QStringLiteral(":/mahali/style.qss"));
    if (style.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(style.readAll()));
    }

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

    // Login flow
    app::data::UserRepository userRepo(*db);
    app::ui::LoginDialog login(*db);
    if (login.exec() != QDialog::Accepted) {
        return 0;
    }

    app::ui::ServerController controller(*db, hmacKey);
    controller.start();

    app::ui::MainWindow window(*db, controller);
    window.show();
    window.raise();
    window.activateWindow();
    return app.exec();
}