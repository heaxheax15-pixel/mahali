#pragma once

#include <QSqlDatabase>
#include <QString>

namespace app::data {

// Journaling policy. The central Windows server DB uses WAL so several phones
// can read while one writes without blocking; the device keeps the classic
// sequential journal, which is equally durable (synchronous = FULL) but leaves
// no companion files on the phone.
enum class DatabaseMode {
    Device,
    Server,
};

class Database {
public:
    explicit Database(const QString& filePath, DatabaseMode mode = DatabaseMode::Device);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    QSqlDatabase handle() const;

    bool beginTransaction();
    bool commit();
    bool rollback();

    QString lastError() const;

    bool verifyStockConsistency() const;
    void recomputeStockQuantities();

private:
    void applyPragmas();
    void createSchema();
    bool execStatements(const QStringList& statements, const QString& source);

    QSqlDatabase m_db;
    DatabaseMode m_mode = DatabaseMode::Device;
    mutable QString m_lastError;
};

} // namespace app::data