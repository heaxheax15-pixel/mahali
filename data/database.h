#pragma once

#include <QSqlDatabase>
#include <QSqlError>
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

    // Records a failed statement. Repositories call this on every exec() that
    // comes back false: without it the driver error is dropped on the floor and
    // the service that asked for the write reports a blank reason, which reads
    // to the operator as "nothing happened" rather than "the database refused".
    // context names the call site, e.g. "ProductRepository::save".
    void recordError(const QSqlError& err, const QString& context);

    // The call site recorded by the last recordError(), empty if the last error
    // came from Database itself rather than from a repository.
    QString lastErrorContext() const;

    // Drops the recorded reason. Called at the start of every operation in this
    // layer, so lastError() always describes the most recent outcome instead of
    // the last failure that happened to still be sitting in the slot: a caller
    // that reads it after a successful write should not be handed a stale
    // reason and conclude the write it just watched succeed had failed.
    // const for the same reason lastError() is: the state is mutable so that
    // const observers (handle()) can reset it without giving up constness.
    void clearError() const;

    bool verifyStockConsistency() const;
    void recomputeStockQuantities();

private:
    void applyPragmas();
    void createSchema();
    bool execStatements(const QStringList& statements, const QString& source);
    void createSingleOpenSessionIndex();

    QSqlDatabase m_db;
    DatabaseMode m_mode = DatabaseMode::Device;
    mutable QString m_lastError;
    mutable QString m_lastErrorContext;
    mutable QSqlError m_lastSqlError;
};

} // namespace app::data