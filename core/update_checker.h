#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace app::core {

// Asks GitHub for the newest published release and compares its tag against the
// version this binary was built with. One check per call; the manager is kept
// alive between calls so a second check() reuses the connection.
//
// Every path ends in exactly one signal — updateAvailable, upToDate or
// checkFailed — so a caller only needs one connection and never has to guard
// against a silent no-op. A failed check is never fatal: the app keeps running
// on whatever version it already has.
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker() override;

    // The release feed this checker queries.
    static QUrl latestReleaseUrl();

    // Seconds to wait for GitHub before giving up. The API is a nice-to-have, so
    // the default is short and a timeout surfaces as checkFailed.
    void setTimeout(int seconds);
    int timeout() const { return m_timeoutSeconds; }

public slots:
    // Starts an asynchronous check. Returns immediately.
    Q_INVOKABLE void check();

signals:
    // A strictly newer release exists. `tag` is the raw release tag,
    // `notes` the release body (empty when the release carries none).
    void updateAvailable(const QString& tag, const QString& notes);

    // The running build is at least as new as the latest release.
    void upToDate();

    // The check could not be completed: no network, timeout, HTTP error, rate
    // limit or an unparseable payload. `reason` is meant for a log line.
    void checkFailed(const QString& reason);

private:
    void handleReply(QNetworkReply* reply);

    QNetworkAccessManager* m_manager;
    int m_timeoutSeconds = 15;
};

} // namespace app::core
