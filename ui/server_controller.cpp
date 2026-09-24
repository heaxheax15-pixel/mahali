#include "server_controller.h"

#include <QDateTime>
#include <QHostAddress>
#include <QSqlQuery>

#include "data/cash_movement_repository.h"
#include "data/cash_session_repository.h"
#include "data/sale_repository.h"

namespace app::ui {

ServerController::ServerController(app::data::Database& db, const QByteArray& hmacKey, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_hmacKey(hmacKey)
    , m_server(db, hmacKey)
{
    m_statsTimer.setInterval(2000);
    connect(&m_statsTimer, &QTimer::timeout, this, &ServerController::refreshStats);
}

bool ServerController::start()
{
    return startAt(0);
}

bool ServerController::startAt(quint16 port)
{
    if (m_port != 0) {
        return true; // already listening
    }
    // Any == the shop LAN (plus loopback for tests). The store clock sits on one
    // network; phones reach the PC by its LAN address.
    m_port = m_server.server().listen(QHostAddress::Any, port);
    if (m_port == 0) {
        emit message(QStringLiteral("تعذر تشغيل خادم المزامنة"));
        return false;
    }
    refreshStats();
    m_statsTimer.start();
    emit message(QStringLiteral("خادم المزامنة يعمل على المنفذ %1").arg(m_port));
    return true;
}

void ServerController::stop()
{
    m_statsTimer.stop();
    m_server.server().disconnect();
    m_port = 0;
    refreshStats();
    emit message(QStringLiteral("تم إيقاف خادم المزامنة"));
}

bool ServerController::isListening() const
{
    return m_port != 0;
}

quint16 ServerController::port() const
{
    return m_port;
}

ServerController::Stats ServerController::stats() const
{
    return m_stats;
}

int ServerController::runRetention(int days)
{
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-days);
    app::data::AppliedOpRepository journal(m_db);
    const int pruned = journal.pruneOlderThan(cutoff);
    emit message(QStringLiteral("تم تنظيف سجل العمليات: %1 صفاً أقدم من %2 يوم").arg(pruned).arg(days));
    return pruned;
}

void ServerController::setStatsIntervalMs(int msec)
{
    m_statsTimer.setInterval(msec);
}

void ServerController::refreshStats()
{
    app::data::AppliedOpRepository journal(m_db);
    m_stats.appliedOps = journal.count();

    QSqlQuery devices(m_db.handle());
    m_stats.devices = 0;
    if (devices.exec(QStringLiteral("SELECT COUNT(DISTINCT device_id) FROM applied_ops"))
        && devices.next()) {
        m_stats.devices = devices.value(0).toInt();
    }

    const QDateTime today = QDateTime::currentDateTime();
    QDateTime startOfToday = today;
    startOfToday.setTime(QTime(0, 0, 0));

    app::data::SaleRepository sales(m_db);
    const auto todays = sales.findBetween(startOfToday, today);
    m_stats.salesToday = static_cast<int>(todays.size());
    long long revenue = 0;
    for (const core::Sale& sale : todays) {
        revenue += sale.totalCents;
    }
    m_stats.revenueTodayCents = revenue;

    m_stats.listening = m_port != 0;
    m_stats.port = m_port;

    emit statsChanged();
}

} // namespace app::ui