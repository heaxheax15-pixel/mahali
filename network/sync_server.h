#pragma once

#include <QByteArray>
#include <QHttpServer>
#include <QHttpServerResponder>

#include "data/database.h"

namespace app::network {

// HTTP front-end for the local API server. Owns a QHttpServer and an
// HMAC-authenticated /api/sync endpoint that bulk-applies phone operations to
// the Windows database, returning a per-operation ACK list.
class SyncServer {
public:
    explicit SyncServer(app::data::Database& db, const QByteArray& hmacKey = QByteArray());

    QHttpServer& server();

private:
    app::data::Database& m_db;
    QByteArray m_hmacKey;
    QHttpServer m_server;
};

} // namespace app::network