#include "sync_server.h"

#include <QDateTime>
#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "sync_processor.h"
#include "sync_protocol.h"

namespace app::network {

SyncServer::SyncServer(app::data::Database& db, const QByteArray& hmacKey)
    : m_db(db)
    , m_hmacKey(hmacKey)
{
    // /api/sync — receive a batch of operations, verify authenticity, apply each
    // in its own transaction, and return the per-op ACK list.
    m_server.route("/api/sync",
                   [this](const QHttpServerRequest& request, QHttpServerResponder&& responder) {
                       const QByteArray body = request.body();
                       const QByteArray signature = QByteArray::fromHex(request.value("X-Mahali-Signature"));
                       SyncProcessor processor(m_db);
                       const SyncBatchResult result = processor.process(body, signature, m_hmacKey);

                       QJsonObject payload;
                       payload.insert(QStringLiteral("ok"), result.hmacValid && result.batchValid);
                       if (!result.error.isEmpty()) {
                           payload.insert(QStringLiteral("error"), result.error);
                       }

                       QJsonArray applied;
                       for (const SyncAppliedOp& op : result.applied) {
                           QJsonObject ack;
                           ack.insert(QStringLiteral("opId"), op.opId);
                           ack.insert(QStringLiteral("type"), op.type);
                           ack.insert(QStringLiteral("entityId"), op.entityId);
ack.insert(QStringLiteral("totalCents"),
                                      QJsonValue(static_cast<double>(op.totalCents)));
                            ack.insert(QStringLiteral("cogsCents"),
                                      QJsonValue(static_cast<double>(op.cogsCents)));
                            if (op.alreadyApplied) {
                                ack.insert(QStringLiteral("alreadyApplied"), true);
                            }
                            applied.append(ack);
                       }
                       payload.insert(QStringLiteral("applied"), applied);

QJsonArray errors;
                        for (const SyncOpError& op : result.errors) {
                            QJsonObject error;
                            error.insert(QStringLiteral("opId"), op.opId);
                            // Per-op error class travels with the ACK so the
                            // device treats transient gaps ("no open cash
                            // session") as retryable, unlike hard rejections.
                            error.insert(QStringLiteral("errorClass"),
                                         static_cast<int>(op.errorClass));
                            error.insert(QStringLiteral("message"), op.message);
                            errors.append(error);
                        }
                       payload.insert(QStringLiteral("errors"), errors);

                       if (!result.hmacValid) {
                           responder.write(QJsonDocument(payload),
                                           QHttpServerResponder::StatusCode::Forbidden);
                           return;
                       }
                       if (!result.batchValid) {
                           responder.write(QJsonDocument(payload),
                                           QHttpServerResponder::StatusCode::BadRequest);
                           return;
                       }
                       responder.write(QJsonDocument(payload), QHttpServerResponder::StatusCode::Ok);
                   });

    // /api/health — liveness probe used by the phone before sending batches.
    m_server.route("/api/health",
                   [this](const QHttpServerRequest&, QHttpServerResponder&& responder) {
                       QJsonObject payload;
                       payload.insert(QStringLiteral("status"), QStringLiteral("ok"));
                       payload.insert(QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
                       responder.write(QJsonDocument(payload), QHttpServerResponder::StatusCode::Ok);
                   });
}

QHttpServer& SyncServer::server()
{
    return m_server;
}

} // namespace app::network