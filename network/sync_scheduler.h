#pragma once

#include <QObject>
#include <QTimer>
#include <QUrl>

#include "data/database.h"
#include "sync_coordinator.h"

Q_DECLARE_METATYPE(app::network::SyncDrainResult)

namespace app::network {

// Device-side automatic sync loop. Owns the SyncCoordinator and drains the
// outbox on a timer, growing an exponential backoff whenever a round makes no
// full progress (server unreachable, retryable per-op errors) and resetting it
// the moment everything reconciles. The loop itself is deliberately dumb: it
// asks the coordinator exactly what is still pending and trusts the durable
// journal, so a device can sit in a dead LAN indefinitely and simply catch up
// when the server returns — Draining in the background, never losing a row.
class SyncScheduler : public QObject {
    Q_OBJECT

public:
    SyncScheduler(app::data::Database& db, const QUrl& endpoint, const QByteArray& hmacKey,
                  QObject* parent = nullptr);

    // Tunables in milliseconds. Tests drive these down to keep them fast.
    void setBaseInterval(int msec);
    void setMaxInterval(int msec);

    void start();
    void stop();

    // Immediate out-of-band drain (e.g. right after recording a sale), with the
    // timer then rescheduled with the resulting interval.
    void syncNow();

    int pendingCount() const;
    int currentInterval() const;
    int baseIntervalForTest() const;

signals:
    void syncCompleted(const SyncDrainResult& result);

private slots:
    void onTick();

private:
    void runDrain();
    int m_currentInterval = 0;

    SyncCoordinator m_coordinator;
    QTimer m_timer;
    int m_baseInterval = 3000;
    int m_maxInterval = 300000;
};

} // namespace app::network