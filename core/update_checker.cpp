#include "update_checker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "version.h"

namespace app::core {

namespace {

// The release the installer is published from. The zip is Mahali-Setup.zip, the
// same artefact the Windows workflow uploads.
const char* kLatestReleaseUrl =
    "https://api.github.com/repos/heaxheax15-pixel/mahali/releases/latest";

// GitHub rejects API calls without a User-Agent, and answers with a different
// payload shape unless the API version is pinned.
const char* kAcceptHeader = "application/vnd.github+json";

QString userAgent()
{
    const Version version = currentVersion();
    return QStringLiteral("Mahali/%1.%2.%3")
        .arg(version.major)
        .arg(version.minor)
        .arg(version.patch);
}

} // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
}

UpdateChecker::~UpdateChecker() = default;

QUrl UpdateChecker::latestReleaseUrl()
{
    return QUrl(QString::fromLatin1(kLatestReleaseUrl));
}

void UpdateChecker::setTimeout(int seconds)
{
    m_timeoutSeconds = qMax(1, seconds);
}

void UpdateChecker::check()
{
    QNetworkRequest request(latestReleaseUrl());
    request.setRawHeader("Accept", kAcceptHeader);
    request.setRawHeader("User-Agent", userAgent().toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_timeoutSeconds * 1000);

    QNetworkReply* reply = m_manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleReply(reply); });
}

void UpdateChecker::handleReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // Covers offline, DNS failure, timeout and HTTP status errors alike: the
        // app stays on its current version either way.
        emit checkFailed(reply->errorString());
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    if (!document.isObject()) {
        emit checkFailed(QStringLiteral("unexpected release payload"));
        return;
    }

    const QJsonObject release = document.object();
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty()) {
        emit checkFailed(QStringLiteral("release payload has no tag_name"));
        return;
    }

    if (!isNewer(parseVersion(tag), currentVersion())) {
        emit upToDate();
        return;
    }

    emit updateAvailable(tag, release.value(QStringLiteral("body")).toString());
}

} // namespace app::core
