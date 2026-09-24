#include <QApplication>
#include <QDir>
#include <QMessageBox>

#include "data/database.h"
#include "data/setting_repository.h"
#include "ui/format_utils.h"
#include "ui/main_window.h"
#include "ui/server_controller.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mahali"));
    app.setLayoutDirection(Qt::RightToLeft);

    const QString dbPath = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                    : QDir::home().filePath(QStringLiteral("mahali.sqlite"));

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

    app::ui::ServerController controller(*db, hmacKey);
    controller.start();

    app::ui::MainWindow window(*db, controller);
    window.show();
    return app.exec();
}