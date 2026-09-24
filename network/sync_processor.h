#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QString>
#include <QVector>

#include "data/applied_op_repository.h"
#include "data/database.h"
#include "data/payment_service.h"
#include "data/sale_service.h"

namespace app::network {

struct SyncAppliedOp {
    int opId = 0;
    QString type;
    int entityId = 0;
    long long totalCents = 0;
    long long cogsCents = 0;
    bool alreadyApplied = false;
};

struct SyncOpError {
    int opId = 0;
    QString message;
};

struct SyncBatchResult {
    bool hmacValid = false;
    bool batchValid = false;
    QString error;
    QVector<SyncAppliedOp> applied;
    QVector<SyncOpError> errors;
};

// Applies a batch of sync operations against the Windows-side database.
// Each operation is committed in its own single transaction; the result is a
// per-operation ACK list. costPrice is always re-derived from product.costPriceCents
// (never trusted from the incoming payload).
class SyncProcessor {
public:
    explicit SyncProcessor(app::data::Database& db);

    SyncBatchResult process(const QByteArray& body, const QByteArray& signature, const QByteArray& key);
    SyncBatchResult processJson(const QJsonArray& ops);

private:
    SyncAppliedOp applySale(const QJsonObject& json, int cashSessionId, QString* error);
    SyncAppliedOp applyDebt(const QJsonObject& json, QString* error);
    SyncAppliedOp applyPayment(const QJsonObject& json, int cashSessionId, QString* error);

    app::data::Database& m_db;
    app::data::SaleService m_sales;
    app::data::PaymentService m_payments;
    app::data::AppliedOpRepository m_appliedOps;
};

} // namespace app::network