#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QStandardPaths>

#include "data/database.h"
#include "data/setting_repository.h"
#include "ui/format_utils.h"
#include "ui/main_window.h"
#include "ui/server_controller.h"
#include "ui/theme.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mahali"));
    app.setApplicationDisplayName(QStringLiteral("محلي"));
    app.setOrganizationName(QStringLiteral("mahali"));
    app.setApplicationVersion(QStringLiteral(MAHALI_VERSION));
    app.setLayoutDirection(Qt::RightToLeft);

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
        QMessageBox::critical(nullptr, QStringLiteral("محلي — خطأ"),
                              QStringLiteral("تعذر فتح قاعدة البيانات:\n%1").arg(QString::fromUtf8(e.what())));
        return 1;
    }

    // The sync key is set once in Settings (Phase 14); default for first launch.
    app::data::SettingRepository settings(*db);
    const QByteArray hmacKey = settings.value(QStringLiteral("sync_hmac_key"))
                                   .value_or(QStringLiteral("mahali-local-key"))
                                   .toUtf8();

    app::ui::setCurrencySymbol(settings.value(QStringLiteral("currency_symbol")).value_or(QString()));

    // Apply the stored theme before the window appears (default: light).
    app::ui::applyTheme(settings.value(QStringLiteral("theme")).value_or(QStringLiteral("light")), app);

    app::ui::ServerController controller(*db, hmacKey);
    controller.start();

    app::ui::MainWindow window(*db, controller);
    window.show();
    return app.exec();
}