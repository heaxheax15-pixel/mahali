#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace app::core {

// Fetches the published installer and leaves it in the temp directory.
//
// Downloading is deliberately split from installing: this class only puts bytes
// on disk and reports how it went, and a separate, platform-specific step takes
// over once the app has quit. A download that dies half way is a discarded file
// and a message, never a half-replaced application.
//
// The bytes themselves are not verified here — no checksum, no signature. The
// URL is pinned to a fixed GitHub release asset over HTTPS, so the transport
// carries the trust, not this class.
class UpdateDownloader : public QObject {
    Q_OBJECT

public:
    explicit UpdateDownloader(QObject* parent = nullptr);
    ~UpdateDownloader() override;

    // The release asset published by the Windows workflow. Kept as a static so
    // the settings page, the banner and the tests all agree on it.
    static QUrl defaultUrl();

    // Where the archive is written. Any earlier file with this name is removed
    // before a download starts, so a stale copy can never be reported as
    // success.
    static QString defaultDestination();

    // Setters exist so a download can be pointed at a mirror, or at a port
    // where nothing answers when the failure path is under test.
    void setUrl(const QUrl& url) { m_url = url; }
    QUrl url() const { return m_url; }

    void setDestination(const QString& path) { m_destination = path; }
    QString destination() const { return m_destination; }

    // Ceiling on the whole transfer, not on the silence between packets. A
    // shop on a slow link can legitimately take minutes; this exists only to
    // stop a dead connection from leaving the banner stuck at one percentage.
    void setTimeout(int seconds);
    int timeout() const { return m_timeoutSeconds; }

public slots:
    // Starts the download. Returns immediately; ignored if one is already in
    // flight, so a double click cannot start two.
    void start();

signals:
    // Bytes received so far, as a whole percentage. Reported as 0 while the
    // total size is still unknown rather than as a fabricated number.
    void progress(int percent);

    // The archive is on disk at `path`.
    void finished(const QString& path);

    // Nothing usable was written. `reason` is meant for a log line, not for
    // the user.
    void failed(const QString& reason);

private:
    void handleReadyRead();
    void handleProgress(qint64 received, qint64 total);
    void handleFinished();

    QNetworkAccessManager* m_manager;
    QFile* m_file = nullptr;
    QNetworkReply* m_reply = nullptr;
    QUrl m_url;
    QString m_destination;
    int m_timeoutSeconds = 900;
};

} // namespace app::core
