#include "sync_sequence_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

SyncSequenceRepository::SyncSequenceRepository(Database& db)
    : m_db(db)
{
}

int SyncSequenceRepository::nextOpId()
{
    QSqlQuery update(m_db.handle());
    update.prepare(QStringLiteral("UPDATE sync_sequence SET value = value + 1 WHERE id = 1"));
    if (!update.exec() || update.numRowsAffected() != 1) {
        return 0;
    }

    QSqlQuery select(m_db.handle());
    select.prepare(QStringLiteral("SELECT value FROM sync_sequence WHERE id = 1"));
    if (!select.exec() || !select.next()) {
        return 0;
    }
    return select.value(0).toInt();
}

} // namespace app::data