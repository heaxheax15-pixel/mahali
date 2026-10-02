// Dev tool: renders each MainWindow page and saves a PNG per page so the UI
// can be reviewed without a display. Built only when MAHALI_TOOLS=ON.
#include <QApplication>
#include <QAbstractScrollArea>
#include <QCoreApplication>
#include <QDir>
#include <QDialog>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontMetrics>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QStyleOptionButton>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
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
#include "ui/login_dialog.h"
#include "ui/dialogs/customer_dialog.h"
#include "ui/dialogs/product_dialog.h"
#include "ui/dialogs/purchase_dialog.h"
#include "ui/dialogs/supplier_dialog.h"
#include "ui/dialogs/supplier_payment_dialog.h"
#include "ui/dialogs/supplier_return_dialog.h"

#include <cstdio>
#include <functional>
#include <memory>

namespace {

const char* kPageNames[] = {
    "pos", "products", "customers", "suppliers", "cash-session",
    "sales", "expenses", "reports", "refunds", "audit-log", "users",
    "purchases", "occasions", "settings", "stock"};
constexpr int kPageCount = static_cast<int>(std::size(kPageNames));
constexpr QSize kCaptureSizes[] = {
    QSize(1024, 600), QSize(1366, 768), QSize(1920, 1080)};

QString widgetLabel(const QWidget* widget)
{
    const QString name = widget->objectName().isEmpty()
        ? QString::fromLatin1(widget->metaObject()->className())
        : widget->objectName();
    return QStringLiteral("%1(%2)")
        .arg(QString::fromLatin1(widget->metaObject()->className()), name);
}

qsizetype reportLayoutDiagnostics(QWidget* root, const QString& context)
{
    qsizetype issues = 0;
    QList<QWidget*> widgets = root->findChildren<QWidget*>();
    widgets.prepend(root);

    for (QWidget* widget : widgets) {
        if (!widget->isVisible()) {
            continue;
        }

        QString text;
        QRect textArea;
        bool wraps = false;
        if (auto* button = qobject_cast<QPushButton*>(widget)) {
            text = button->text();
            QStyleOptionButton option;
            option.initFrom(button);
            option.text = text;
            option.icon = button->icon();
            option.iconSize = button->iconSize();
            textArea = button->style()->subElementRect(
                QStyle::SE_PushButtonContents, &option, button);
            if (!button->icon().isNull()) {
                textArea.setWidth(textArea.width() - button->iconSize().width() - 4);
            }
        } else if (auto* toolButton = qobject_cast<QToolButton*>(widget)) {
            text = toolButton->text();
            textArea = toolButton->contentsRect().adjusted(3, 3, -3, -3);
            wraps = toolButton->toolButtonStyle() == Qt::ToolButtonTextUnderIcon;
        } else if (auto* label = qobject_cast<QLabel*>(widget)) {
            text = label->text();
            textArea = label->contentsRect();
            wraps = label->wordWrap();
        }

        if (!text.isEmpty() && textArea.isValid()) {
            const QFontMetrics metrics(widget->font());
            const int requiredWidth = metrics.horizontalAdvance(text);
            const QRect measured = wraps
                ? metrics.boundingRect(QRect(0, 0, textArea.width(), 10000),
                                       Qt::TextWordWrap, text)
                : QRect(0, 0, requiredWidth, metrics.height());
            if ((!wraps && requiredWidth > textArea.width())
                || measured.height() > textArea.height()) {
                ++issues;
                qInfo().noquote() << "DIAGNOSTIC TEXT_CLIPPED" << context
                                  << widgetLabel(widget)
                                  << "required=" << requiredWidth << 'x' << measured.height()
                                  << "available=" << textArea.width() << 'x' << textArea.height()
                                  << "text=" << text;
            }
        }

        QWidget* parent = widget->parentWidget();
        if (QString::fromLatin1(widget->metaObject()->className())
                == QStringLiteral("QLineEditIconButton")) {
            continue;
        }
        const bool scrollContent = parent
            && (qobject_cast<QAbstractScrollArea*>(parent)
                || qobject_cast<QAbstractScrollArea*>(parent->parentWidget()));
        if (parent && !widget->isWindow() && !scrollContent
            && !parent->rect().contains(widget->geometry())) {
            ++issues;
            qInfo().noquote() << "DIAGNOSTIC OUTSIDE_PARENT" << context
                              << widgetLabel(widget)
                              << "geometry=" << widget->geometry()
                              << "parent=" << widgetLabel(parent)
                              << "parentRect=" << parent->rect();
        }
    }

    return issues;
}

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
    const bool diagnose = qEnvironmentVariableIsSet("MAHALI_UI_DIAGNOSTICS");
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

    if (measure) {
        for (const QString& pragmaName : {QStringLiteral("journal_mode"),
                                          QStringLiteral("synchronous"),
                                          QStringLiteral("cache_size"),
                                          QStringLiteral("temp_store")}) {
            QSqlQuery pragma(db->handle());
            if (pragma.exec(QStringLiteral("PRAGMA %1").arg(pragmaName)) && pragma.next()) {
                qInfo().noquote() << "DB_PRAGMA" << pragmaName << '=' << pragma.value(0).toString();
            }
        }
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

    if (argc > 4 && QString::fromLocal8Bit(argv[4]) == QStringLiteral("productbench")) {
        const int productCount = argc > 5 ? QString::fromLocal8Bit(argv[5]).toInt() : 5000;
        QSqlDatabase connection = db->handle();
        if (!connection.transaction()) {
            qInfo().noquote() << "BENCH seed_failed=" << connection.lastError().text();
            return 2;
        }
        QSqlQuery insert(connection);
        insert.prepare(QStringLiteral(
            "INSERT INTO products "
            "(barcode, name, cost_price_cents, sale_price_cents, quantity, unit, package_size, active, sold_by_weight) "
            "VALUES (?, ?, 100, 200, 10, 'piece', 1, 1, 0)"));
        for (int index = 0; index < productCount; ++index) {
            insert.addBindValue(QStringLiteral("BENCH-%1").arg(index, 8, 10, QLatin1Char('0')));
            insert.addBindValue(QStringLiteral("Bench-Product-%1").arg(index, 8, 10, QLatin1Char('0')));
            if (!insert.exec()) {
                qInfo().noquote() << "BENCH seed_failed_at=" << index
                                  << "error=" << insert.lastError().text();
                connection.rollback();
                return 2;
            }
        }
        if (!connection.commit()) {
            qInfo().noquote() << "BENCH commit_failed=" << connection.lastError().text();
            return 2;
        }

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
            return 3;
        }
        qInfo().noquote() << "BENCH products=" << productCount
                          << "page_build_ns=" << pageBuildNs
                          << "first_display_ns=" << firstDisplayNs
                          << "widgets=" << page.findChildren<QWidget*>().size();
        for (const QString& query : {QStringLiteral("Bench-Product-00000001"),
                                     QStringLiteral("Bench-Product-%1")
                                         .arg(productCount - 1, 8, 10, QLatin1Char('0'))}) {
            pageTimer.restart();
            search->setText(query);
            QCoreApplication::processEvents();
            qInfo().noquote() << "BENCH search=" << query
                              << "elapsed_ns=" << pageTimer.nsecsElapsed()
                              << "matches=" << products->model()->rowCount();
        }
        return 0;
    }

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
    QCoreApplication::processEvents();

    if (diagnose) {
        const auto inspectModal = [&app](const QString& name, const std::function<void()>& open) {
            bool inspected = false;
            QTimer::singleShot(0, &app, [&app, &inspected, name]() {
                for (QWidget* topLevel : QApplication::topLevelWidgets()) {
                    auto* dialog = qobject_cast<QDialog*>(topLevel);
                    if (!dialog || !dialog->isVisible()) {
                        continue;
                    }
                    const qsizetype issues = reportLayoutDiagnostics(dialog, name);
                    qInfo().noquote() << "DIAGNOSTIC DIALOG" << name
                                      << "minimumSizeHint=" << dialog->minimumSizeHint()
                                      << "size=" << dialog->size()
                                      << "issues=" << issues;
                    inspected = true;
                    dialog->reject();
                    return;
                }
                qInfo().noquote() << "DIAGNOSTIC DIALOG_NOT_SHOWN" << name;
            });
            open();
            QCoreApplication::processEvents();
            if (!inspected) {
                qInfo().noquote() << "DIAGNOSTIC DIALOG_NOT_INSPECTED" << name;
            }
        };

        inspectModal(QStringLiteral("login"), [&]() {
            app::ui::LoginDialog dialog(*db, &window);
            dialog.exec();
        });
        app::core::Product productDraft;
        productDraft.salePriceCents = 100;
        inspectModal(QStringLiteral("product"), [&]() {
            app::ui::showProductDialog(&window, *db, productDraft);
        });
        inspectModal(QStringLiteral("customer"), [&]() {
            app::ui::showCustomerInfoDialog(&window, *db, app::core::Customer{});
        });
        inspectModal(QStringLiteral("supplier"), [&]() {
            app::ui::showSupplierInfoDialog(&window, *db, app::core::Supplier{});
        });
        inspectModal(QStringLiteral("purchase"), [&]() {
            app::ui::showPurchaseDialog(&window, *db, 0);
        });
        inspectModal(QStringLiteral("supplier-payment"), [&]() {
            app::ui::showSupplierPaymentDialog(&window, *db, 0);
        });
        inspectModal(QStringLiteral("supplier-return"), [&]() {
            app::ui::showSupplierReturnDialog(&window, *db, 0);
        });
    }

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
        const QStackedWidget* pageStack = window.findChild<QStackedWidget*>(QStringLiteral("content"));
        if (diagnose) {
            const QSize minimum = pageStack && pageStack->currentWidget()
                ? pageStack->currentWidget()->minimumSizeHint()
                : QSize();
            qInfo().noquote() << "DIAGNOSTIC PAGE_MINIMUM"
                              << QLatin1String(kPageNames[page])
                              << "viewport=" << kCaptureSizes[sizeIndex]
                              << "minimumSizeHint=" << minimum
                              << "exceeds=" << (minimum.width() > kCaptureSizes[sizeIndex].width()
                                                  || minimum.height() > kCaptureSizes[sizeIndex].height());
            const qsizetype issueCount = reportLayoutDiagnostics(
                window.centralWidget(),
                QStringLiteral("%1 %2x%3")
                    .arg(QLatin1String(kPageNames[page]))
                    .arg(kCaptureSizes[sizeIndex].width())
                    .arg(kCaptureSizes[sizeIndex].height()));
            qInfo().noquote() << "DIAGNOSTIC SUMMARY"
                              << QLatin1String(kPageNames[page])
                              << "issues=" << issueCount;
            for (QWidget* topLevel : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QDialog*>(topLevel); dialog && dialog->isVisible()) {
                    qInfo().noquote() << "DIAGNOSTIC DIALOG"
                                      << widgetLabel(dialog)
                                      << "minimumSizeHint=" << dialog->minimumSizeHint()
                                      << "size=" << dialog->size()
                                      << "issues=" << reportLayoutDiagnostics(dialog, QStringLiteral("dialog"));
                }
            }
        }
        const QPixmap pageImage = window.grab();
        const qint64 firstDisplayNs = pageTimer.nsecsElapsed();
        const qsizetype widgetCount = window.findChildren<QWidget*>().size();
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