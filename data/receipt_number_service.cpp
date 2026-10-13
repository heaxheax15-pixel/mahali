#include "receipt_number_service.h"

#include <QDate>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "database.h"
#include "setting_repository.h"

namespace app::data {

QString currentDevicePrefix(Database& db)
{
    SettingRepository settings(db);
    const QString prefix = settings.value(QStringLiteral("device_prefix")).value_or(QString());
    if (prefix.isEmpty()) {
        return QStringLiteral("A");
    }
    return prefix;
}

QString mintReceiptNumber(Database& db)
{
    const int year = QDate::currentDate().year();

    // Three statements, all inside the caller's transaction. The row is created
    // if the year is new, the counter is bumped, and the bumped value is read
    // back: the increment is an UPDATE rather than a read-modify-write in C++ so
    // that two mints on one connection cannot lose one another's increment.
    {
        QSqlQuery seed(db.handle());
        seed.prepare(QStringLiteral("INSERT OR IGNORE INTO receipt_sequences (year, last_number) "
                                    "VALUES (?, 0)"));
        seed.addBindValue(year);
        if (!seed.exec()) {
            db.recordError(seed.lastError(), QStringLiteral("mintReceiptNumber seed"));
            return QString();
        }
    }
    {
        QSqlQuery bump(db.handle());
        bump.prepare(QStringLiteral("UPDATE receipt_sequences SET last_number = last_number + 1 "
                                    "WHERE year = ?"));
        bump.addBindValue(year);
        if (!bump.exec()) {
            db.recordError(bump.lastError(), QStringLiteral("mintReceiptNumber bump"));
            return QString();
        }
    }

    QSqlQuery read(db.handle());
    read.prepare(QStringLiteral("SELECT last_number FROM receipt_sequences WHERE year = ?"));
    read.addBindValue(year);
    if (!read.exec() || !read.next()) {
        db.recordError(read.lastError(), QStringLiteral("mintReceiptNumber read"));
        return QString();
    }
    const int counter = read.value(0).toInt();

    return currentDevicePrefix(db) + QStringLiteral("-") + QString::number(year) + QStringLiteral("-")
           + QStringLiteral("%1").arg(counter, 4, 10, QLatin1Char('0'));
}

} // namespace app::data
