// Dev tool: renders each MainWindow page and saves a PNG per page so the UI
// can be reviewed without a display. Built only when MAHALI_TOOLS=ON.
#include <QApplication>
#include <QCoreApplication>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QTimer>
#include <QWidget>

#include "data/database.h"
#include "data/setting_repository.h"
#include "ui/format_utils.h"
#include "ui/main_window.h"
#include "ui/server_controller.h"
#include "ui/theme.h"

#include <cstdio>
#include <functional>
#include <memory>

namespace {

const char* kPageNames[] = {
    "pos", "products", "customers", "suppliers", "cash-session",
    "sales", "expenses", "reports", "refunds", "audit-log", "settings"};

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mahali"));
    app.setApplicationDisplayName(QCoreApplication::translate("main", "محلي"));
    app.setLayoutDirection(Qt::RightToLeft);

    const QString dbPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(":memory:");
    const QString outDir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("shots");

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

    app::data::SettingRepository settings(*db);
    const QByteArray hmacKey = settings.value(QStringLiteral("sync_hmac_key"))
                                   .value_or(QStringLiteral("mahali-local-key"))
                                   .toUtf8();
    app::ui::setCurrencySymbol(settings.value(QStringLiteral("currency_symbol")).value_or(QString()));
    app::ui::applyTheme(settings.value(QStringLiteral("theme")).value_or(QStringLiteral("light")), app);

    app::ui::ServerController controller(*db, hmacKey);
    controller.start();

    app::ui::MainWindow window(*db, controller);
    window.resize(1360, 840);
    window.show();

    int page = 0;
    std::function<void()> step = [&]() {
        if (page >= 11) {
            QCoreApplication::quit();
            return;
        }
        if (auto* nav = window.findChild<QListWidget*>(QStringLiteral("nav"))) {
            nav->setCurrentRow(page);
        }
        QCoreApplication::processEvents();
        const QString file =
            QStringLiteral("%1/%2.png").arg(outDir).arg(QLatin1String(kPageNames[page]));
        const bool ok = window.grab().save(file);
        std::fprintf(stderr, "%s: %s\n", qPrintable(file), ok ? "ok" : "SAVE FAILED");
        ++page;
        QTimer::singleShot(350, step);
    };
    QTimer::singleShot(400, step);

    return app.exec();
}