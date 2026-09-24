#include "sync_client.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "core/sync_operation_codec.h"

namespace app::network {

SyncSendResult SyncClient::sendBatch(const QUrl& endpoint, const QByteArray& hmacKey,
                                     const QVector<app::core::SyncOperation>& ops)
{
    SyncSendResult result;
    if (ops.isEmpty()) {
        result.errorClass = SyncErrorClass::Permanent;
        result.message = QStringLiteral("empty batch");
        return result;
    }
    if (ops.size() > kMaxBatchSize) {
        result.errorClass = SyncErrorClass::Permanent;
        result.message = QStringLiteral("batch exceeds 50 operations");
        return result;
    }

    QJsonArray body;
    for (const app::core::SyncOperation& op : ops) {
        body.append(QJsonDocument::fromJson(app::core::SyncOpCodec::serialize(op)).object());
    }
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const QByteArray signature = SyncProtocol::hmacSha256(payload, hmacKey);

    QNetworkAccessManager manager;
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("X-Mahali-Signature", signature.toHex());

    QNetworkReply* reply = manager.post(request, payload);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray responseBody = reply->readAll();
    const auto networkError = reply->error();
    const QString networkErrorText = reply->errorString();
    reply->deleteLater();

    if (networkError != QNetworkReply::NoError && status == 0) {
        result.errorClass = SyncErrorClass::Network;
        result.message = networkErrorText;
        return result;
    }

    result.delivered = true;
    result.errorClass = SyncProtocol::errorClassForStatus(status);
    const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
    if (doc.isObject() && doc.object().contains(QStringLiteral("error"))) {
        result.message = doc.object().value(QStringLiteral("error")).toString();
    } else {
        result.message = QStringLiteral("HTTP %1").arg(status);
    }

    if (doc.isObject()) {
        const QJsonArray applied = doc.object().value(QStringLiteral("applied")).toArray();
        const QJsonArray errors = doc.object().value(QStringLiteral("errors")).toArray();
        for (const QJsonValue& value : applied) {
            SyncAck ack;
            ack.ok = true;
            ack.appliedOpId = value.toObject().value(QStringLiteral("opId")).toInt();
            ack.errorClass = SyncErrorClass::None;
            result.acks.append(ack);
        }
        for (const QJsonValue& value : errors) {
            SyncAck ack;
            ack.ok = false;
            ack.appliedOpId = value.toObject().value(QStringLiteral("opId")).toInt();

            // Prefer the per-op error class the server attached (so, e.g., "no
            // open cash session" is retried rather than canned), falling back to
            // the batch-level class for older/terser responses.
            const QJsonValue perOpClass = value.toObject().value(QStringLiteral("errorClass"));
            ack.errorClass = perOpClass.isDouble()
                ? static_cast<SyncErrorClass>(perOpClass.toInt())
                : result.errorClass;
            ack.message = value.toObject().value(QStringLiteral("message")).toString();
            result.acks.append(ack);
        }
    }
    return result;
}

} // namespace app::network