#include "sync_scheduler.h"

namespace app::network {

SyncScheduler::SyncScheduler(app::data::Database& db, const QUrl& endpoint, const QByteArray& hmacKey,
                             QObject* parent)
    : QObject(parent)
    , m_coordinator(db, endpoint, hmacKey)
{
    m_currentInterval = m_baseInterval;
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &SyncScheduler::onTick);
}

void SyncScheduler::setBaseInterval(int msec)
{
    m_baseInterval = qMax(1, msec);
    // Before the loop starts, the next tick fires at the new base. While the
    // loop is running the current (possibly backed-off) interval is preserved.
    if (!m_timer.isActive()) {
        m_currentInterval = m_baseInterval;
    }
}

void SyncScheduler::setMaxInterval(int msec)
{
    m_maxInterval = qMax(m_baseInterval, msec);
}

void SyncScheduler::start()
{
    if (!m_timer.isActive()) {
        m_timer.start(m_currentInterval);
    }
}

void SyncScheduler::stop()
{
    m_timer.stop();
}

void SyncScheduler::syncNow()
{
    m_timer.stop();
    runDrain();
    if (m_timer.isActive()) {
        return; // runDrain already rescheduled it
    }
    m_timer.start(m_currentInterval);
}

int SyncScheduler::pendingCount() const
{
    return m_coordinator.pendingCount();
}

int SyncScheduler::currentInterval() const
{
    return m_currentInterval;
}

int SyncScheduler::baseIntervalForTest() const
{
    return m_baseInterval;
}

void SyncScheduler::onTick()
{
    runDrain();
}

void SyncScheduler::runDrain()
{
    const SyncDrainResult result = m_coordinator.drainOnce();

    // Grow the interval when this round made no complete headway; reset to the
    // base whenever everything left pending (if anything) reconciles cleanly.
    const bool stalled = result.attempted
        && (!result.delivered || result.errorClass == SyncErrorClass::Network
            || result.errorClass == SyncErrorClass::Retry || result.keptPending > 0);
    if (stalled) {
        m_currentInterval = qMin(m_maxInterval, qMax(m_baseInterval, m_currentInterval * 2));
    } else {
        m_currentInterval = m_baseInterval;
    }
    m_timer.start(m_currentInterval);

    emit syncCompleted(result);
}

} // namespace app::network