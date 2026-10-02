// Dev tool: renders each MainWindow page and saves a PNG per page so the UI
// can be reviewed without a display. Built only when MAHALI_TOOLS=ON.
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QWidget>
#include <QtGlobal>

#include "data/database.h"
#include "data/setting_repository.h"
#include "core/format_utils.h"
#include "core/i18n.h"
#include "core/session.h"
#include "ui/main_window.h"
#include "ui/pos_page.h"
#include "ui/server_controller.h"
#include "ui/theme.h"

#include <cstdio>
#include <functional>
#include <memory>

namespace {

const char* kPageNames[] = {
    "pos", "products", "customers", "suppliers", "cash-session",
    "sales", "expenses", "reports", "refunds", "audit-log", "users",
    "purchases", "occasions", "settings", "stock"};
constexpr int kPageCount = static_cast<int>(std::size(kPageNames));
constexpr QSize kCaptureSizes[] = {QSize(1366, 768), QSize(1920, 1080)};

} // namespace

int main(int argc, char* argv[])
{
    QElapsedTimer bootTimer;
    bootTimer.start();
    // See apps/desktop/main.cpp: app.qrc lives in the static libmahali-ui.a, so
    // the resource has to be initialised explicitly or :/mahali/... stays empty.
    Q_INIT_RESOURCE(app);

    QApplication app(argc, argv);
    const bool measure = qEnvironmentVariableIsSet("MAHALI_UI_PERF");
    app.setApplicationName(QStringLiteral("mahali"));
    app.setApplicationDisplayName(QCoreApplication::translate("main", "محلي"));
    // No direction is forced here. This tool loads no translator, so the text is
    // the source Arabic and Qt's own default for that is right-to-left; the
    // screenshots come out the same as before. The real application calls
    // core::applyLanguage() and inherits its direction from the stored language.

    const QString dbPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(":memory:");
    const QString outDir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("shots");
    QDir().mkpath(outDir);

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
    QElapsedTimer themeTimer;
    themeTimer.start();
    const QString language = argc > 3 ? QString::fromLocal8Bit(argv[3])
                                      : app::core::currentLanguage(*db);
    app::core::applyLanguage(language);
    app::ui::applyTheme(settings.value(QStringLiteral("theme")).value_or(QStringLiteral("light")), app);
    if (measure) {
        qInfo().noquote() << "PERF initial_theme_ns=" << themeTimer.nsecsElapsed();
    }

    app::core::User screenshotUser;
    screenshotUser.name = QStringLiteral("لقطات الاختبار");
    screenshotUser.role = QStringLiteral("admin");
    app::core::Session::instance().setCurrentUser(screenshotUser);

    if (argc > 4 && QString::fromLocal8Bit(argv[4]) == QStringLiteral("posbench")) {
        QElapsedTimer pageTimer;
        pageTimer.start();
        app::ui::PosPage page(*db);
        const qint64 pageBuildNs = pageTimer.nsecsElapsed();
        page.resize(kCaptureSizes[0]);
        page.show();
        QCoreApplication::processEvents();
        const qint64 firstDisplayNs = pageTimer.nsecsElapsed() - pageBuildNs;
        auto* search = page.findChild<QLineEdit*>(QStringLiteral("posCatalogSearch"));
        auto* products = page.findChild<QTableView*>(QStringLiteral("posProductTable"));
        if (!search || !products) {
            return 2;
        }
        qInfo().noquote() << "BENCH product_rows=" << products->model()->rowCount()
                          << "page_build_ns=" << pageBuildNs
                          << "first_display_ns=" << firstDisplayNs
                          << "widgets=" << page.findChildren<QWidget*>().size();
        for (const QString& query : {QStringLiteral("Bench-Product-19999"),
                                     QStringLiteral("Bench-Product-00001")}) {
            pageTimer.restart();
            search->setText(query);
            QCoreApplication::processEvents();
            qInfo().noquote() << "BENCH search=" << query
                              << "elapsed_ns=" << pageTimer.nsecsElapsed()
                              << "matches=" << products->model()->rowCount();
        }
        return 0;
    }

    app::ui::ServerController controller(*db, hmacKey);
    controller.start();

    app::ui::MainWindow window(*db, controller);
    if (measure) {
        qInfo().noquote() << "PERF startup_ns=" << bootTimer.nsecsElapsed();
        const QByteArray stylesheet = app.styleSheet().toUtf8();
        qInfo().noquote() << "PERF qss_bytes=" << stylesheet.size()
                          << "qss_rules=" << stylesheet.count('{');
        themeTimer.restart();
        ::app::ui::applyTheme(QStringLiteral("dark"), app);
        const qint64 darkThemeNs = themeTimer.nsecsElapsed();
        themeTimer.restart();
        ::app::ui::applyTheme(QStringLiteral("light"), app);
        const qint64 lightThemeNs = themeTimer.nsecsElapsed();
        themeTimer.restart();
        app.setStyleSheet(QString());
        const qint64 noQssNs = themeTimer.nsecsElapsed();
        ::app::ui::applyTheme(QStringLiteral("light"), app);
        if (argc > 5 && QString::fromLocal8Bit(argv[5]) == QStringLiteral("noqss")) {
            app.setStyleSheet(QString());
        }
        qInfo().noquote() << "PERF live_theme_dark_ns=" << darkThemeNs
                          << "live_theme_light_ns=" << lightThemeNs
                          << "qss_disabled_ns=" << noQssNs;
    }
    window.resize(kCaptureSizes[0]);
    window.show();

    int page = 0;
    int sizeIndex = 0;
    std::function<void()> step = [&]() {
        if (page >= kPageCount) {
            ++sizeIndex;
            if (sizeIndex >= static_cast<int>(std::size(kCaptureSizes))) {
                QCoreApplication::quit();
                return;
            }
            window.resize(kCaptureSizes[sizeIndex]);
            QCoreApplication::processEvents();
            page = 0;
        }
        QElapsedTimer pageTimer;
        pageTimer.start();
        const bool switched = QMetaObject::invokeMethod(
            &window, "onPageChanged", Qt::DirectConnection, Q_ARG(int, page));
        if (!switched) {
            std::fprintf(stderr, "Could not switch to page %d\n", page);
            QCoreApplication::exit(2);
            return;
        }
        QCoreApplication::processEvents();
        const QPixmap pageImage = window.grab();
        const qint64 firstDisplayNs = pageTimer.nsecsElapsed();
        const qsizetype widgetCount = window.findChildren<QWidget*>().size();
        const QStackedWidget* pageStack = window.findChild<QStackedWidget*>(QStringLiteral("content"));
        const qsizetype pageWidgetCount = pageStack && pageStack->currentWidget()
            ? pageStack->currentWidget()->findChildren<QWidget*>().size() + 1
            : 0;
        if (measure) {
            qInfo().noquote() << "PERF page=" << QLatin1String(kPageNames[page])
                              << "size=" << kCaptureSizes[sizeIndex].width() << 'x'
                              << kCaptureSizes[sizeIndex].height()
                              << "open_and_first_display_ns=" << firstDisplayNs
                              << "window_widgets=" << widgetCount
                              << "page_widgets=" << pageWidgetCount;
        }
        const QString file =
            QStringLiteral("%1/%2x%3/%4.png")
                .arg(outDir)
                .arg(kCaptureSizes[sizeIndex].width())
                .arg(kCaptureSizes[sizeIndex].height())
                .arg(QLatin1String(kPageNames[page]));
        QDir().mkpath(QFileInfo(file).path());
        const bool ok = pageImage.save(file);
        std::fprintf(stderr, "%s: %s\n", qPrintable(file), ok ? "ok" : "SAVE FAILED");
        ++page;
        QTimer::singleShot(30, step);
    };
    QTimer::singleShot(100, step);

    return app.exec();
}