#pragma once

#include <QString>
#include <QVector>

#include "cash_movement_repository.h"
#include "cash_session_repository.h"
#include "customer_transaction_item_repository.h"
#include "customer_transaction_repository.h"
#include "database.h"
#include "payment_repository.h"
#include "product_repository.h"
#include "sale_item_repository.h"
#include "sale_repository.h"
#include "stock_movement_repository.h"
#include "sync_outbox_repository.h"
#include "sync_sequence_repository.h"

namespace app::data {

struct DeviceOpResult {
    bool ok = false;
    int opId = 0;          // durable per-device sync op id that will reach the server
    int entityId = 0;      // local row id (sale / customer transaction / payment)
    int outboxId = 0;      // sync_outbox row carrying this operation
    long long totalCents = 0;
    long long cogsCents = 0;
    QString error;
};

// Offline-first device-side entry point. Every financial write is committed in
// ONE transaction together with:
//   * a freshly minted (durable, monotonic) sync opId, and
//   * the matching sync_outbox row queued for LAN delivery.
// A sudden power cut therefore can never leave money recorded locally that has
// no sync record: either the whole thing commits (locally visible AND queued)
// or nothing does. Oversold sales are rejected here exactly as the Windows
// server rejects them, so a device and the central DB can never diverge.
class DeviceLedgerService {
public:
    DeviceLedgerService(Database& db, const QString& deviceId);

    DeviceOpResult recordSale(const QVector<core::SaleItem>& items, int cashSessionId);
    DeviceOpResult recordCustomerDebt(int customerId, const QVector<core::SaleItem>& items);
    DeviceOpResult recordCustomerPayment(int customerId, long long amountCents, int cashSessionId,
                                         const QString& note);

private:
    int mintAndEnqueue(core::SyncOpType type, int entityId, long long totalCents,
                       const QVector<core::SaleItem>& resolved, const QString& note, QString* error);

    Database& m_db;
    QString m_deviceId;
    ProductRepository m_products;
    SaleRepository m_sales;
    SaleItemRepository m_saleItems;
    CustomerTransactionRepository m_customerTransactions;
    CustomerTransactionItemRepository m_customerTransactionItems;
    StockMovementRepository m_stockMovements;
    CashSessionRepository m_cashSessions;
    CashMovementRepository m_cashMovements;
    PaymentRepository m_payments;
    SyncSequenceRepository m_syncSequence;
    SyncOutboxRepository m_outbox;
};

} // namespace app::data