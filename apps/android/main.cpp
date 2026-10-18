#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QtGlobal>

#include <memory>

#include "android_backend.h"
#include "data/database.h"

int main(int argc, char* argv[])
{
    // QGuiApplication, not QApplication: the whole UI is QML, and QtWidgets
    // would only drag widgets and their styles into the APK for nothing.
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mahali"));
    app.setOrganizationName(QStringLiteral("mahali"));

    // A real install keeps its data in the app-data folder; argv[1] exists so
    // a developer can point a device build at a database pulled from a till.
    QString dbPath;
    if (argc > 1) {
        dbPath = QString::fromLocal8Bit(argv[1]);
    } else {
        const QString dataDir =
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        dbPath = QDir(dataDir).filePath(QStringLiteral("mahali.sqlite"));
    }

    // Device mode, not Server: the phone keeps the plain journal, so no
    // -wal/-shm companion files appear next to the database on a platform
    // where nobody will ever run a checkpoint by hand.
    std::unique_ptr<app::data::Database> db;
    try {
        db = std::make_unique<app::data::Database>(dbPath, app::data::DatabaseMode::Device);
    } catch (const std::exception& e) {
        qCritical("mahali: cannot open database %s: %s", qPrintable(dbPath), e.what());
        return 1;
    }

    AndroidBackend backend(*db);
    // Read the products before the engine loads, so the first frame already
    // has rows to show instead of an empty list waiting for a signal.
    backend.refresh();

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);

    // main.qml of the module qt_add_qml_module registered under the URI
    // "Mahali". QTP0001 is not set anywhere in this project, so the module's
    // resource prefix is "/" and the QML files keep their qml/ path component.
    const QUrl url(QStringLiteral("qrc:/Mahali/qml/main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &app,
                     [url](QObject* obj, const QUrl& objUrl) {
                         if (!obj && url == objUrl)
                             QCoreApplication::exit(-1);
                     },
                     Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
