#pragma once

#include <QSqlDatabase>
#include <QString>

namespace app::data {

class Database {
public:
    explicit Database(const QString& filePath);
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
    mutable QString m_lastError;
};

} // namespace app::data