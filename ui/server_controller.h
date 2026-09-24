#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>

#include "data/applied_op_repository.h"
#include "data/database.h"
#include "network/sync_server.h"

namespace app::ui {

// Headless brain of the Windows central PC: owns the sync HTTP server and
// answers pure-logic questions the UI shows. Kept UI-free on purpose so every
// behaviour here is unit-testable without a display.
class ServerController : public QObject {
    Q_OBJECT

public:
    struct Stats {
        bool listening = false;
        quint16 port = 0;
        int appliedOps = 0;   // exactly-once journal rows (sum of all time)
        int devices = 0;      // distinct device ids that ever synced
        int salesToday = 0;
        long long revenueTodayCents = 0;
    };

    ServerController(app::data::Database& db, const QByteArray& hmacKey, QObject* parent = nullptr);

    // Listens on all interfaces (the shop LAN) on an OS-assigned port.
    bool start();
    bool startAt(quint16 port);
    void stop();

    bool isListening() const;
    quint16 port() const;
    Stats stats() const;

    // Removes the central exactly-once journal rows older than `days`. Returns
    // rows pruned. Call on a schedule (daily) to keep the file lean for years.
    int runRetention(int days);

    // Test/tuning hook: how often stats are polled and broadcast (default 2s).
    void setStatsIntervalMs(int msec);

signals:
    void statsChanged();
    // Human-readable one-liner for the operator (server started on port N, ...).
    void message(const QString& text);

private:
    void refreshStats();

    app::data::Database& m_db;
    QByteArray m_hmacKey;
    network::SyncServer m_server;
    QTimer m_statsTimer;
    quint16 m_port = 0;
    Stats m_stats;
};

} // namespace app::ui