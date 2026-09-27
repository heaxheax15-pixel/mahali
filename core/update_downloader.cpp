#include "update_downloader.h"

#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "version.h"

namespace app::core {

namespace {

// The asset uploaded by the Windows release workflow. The "latest" alias
// resolves server-side, so a new release needs no change in this code.
const char* kDownloadUrl =
    "https://github.com/heaxheax15-pixel/mahali/releases/latest/download/Mahali-Setup.zip";

const char* kArchiveName = "Mahali-Setup.zip";

QString userAgent()
{
    const Version version = currentVersion();
    return QStringLiteral("Mahali/%1.%2.%3")
        .arg(version.major)
        .arg(version.minor)
        .arg(version.patch);
}

} // namespace

UpdateDownloader::UpdateDownloader(QObject* parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
    , m_url(QUrl(QString::fromLatin1(kDownloadUrl)))
    , m_destination(QDir(QDir::tempPath()).filePath(QString::fromLatin1(kArchiveName)))
{
}

UpdateDownloader::~UpdateDownloader()
{
    // The reply is a child and dies with us, but a half-written file must not
    // outlive the downloader: it would look like a finished one.
    if (m_file) {
        m_file->close();
    }
}

QUrl UpdateDownloader::defaultUrl()
{
    return QUrl(QString::fromLatin1(kDownloadUrl));
}

QString UpdateDownloader::defaultDestination()
{
    return QDir(QDir::tempPath()).filePath(QString::fromLatin1(kArchiveName));
}

void UpdateDownloader::setTimeout(int seconds)
{
    m_timeoutSeconds = qMax(1, seconds);
}

void UpdateDownloader::start()
{
    if (m_reply) {
        // Already downloading: a second start() would write two streams into
        // one file.
        return;
    }

    // Anything already at the destination belongs to an earlier attempt.
    QFile::remove(m_destination);

    m_file = new QFile(m_destination, this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(m_file->errorString());
        return;
    }

    QNetworkRequest request(m_url);
    request.setRawHeader("Accept", "application/octet-stream");
    request.setRawHeader("User-Agent", userAgent().toUtf8());
    // GitHub answers /releases/latest/download with a redirect to the asset
    // host; stay on https while following it.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_timeoutSeconds * 1000);

    m_reply = m_manager->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateDownloader::handleReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &UpdateDownloader::handleProgress);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateDownloader::handleFinished);
}

void UpdateDownloader::handleReadyRead()
{
    // Streamed to disk rather than buffered: the installer is large enough
    // that holding it in memory would be a real cost on the machines this app
    // targets.
    m_file->write(m_reply->readAll());
}

void UpdateDownloader::handleProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        emit progress(0);
        return;
    }
    emit progress(static_cast<int>((received * 100) / total));
}

void UpdateDownloader::handleFinished()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    m_file->write(reply->readAll());
    m_file->close();

    if (reply->error() != QNetworkReply::NoError) {
        m_file->remove();
        emit failed(reply->errorString());
        return;
    }

    if (m_file->size() == 0) {
        // A success that wrote nothing is not a success: treating it as one
        // would hand the restart step a truncated file to unpack.
        m_file->remove();
        emit failed(QStringLiteral("the download finished empty"));
        return;
    }

    emit finished(m_destination);
}

} // namespace app::core
