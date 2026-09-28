#include "supplier_payment_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

SupplierPaymentRepository::SupplierPaymentRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::SupplierPayment paymentFromQuery(const QSqlQuery& query)
{
    core::SupplierPayment payment;
    payment.id = query.value(0).toInt();
    payment.supplierId = query.value(1).toInt();
    const int purchase = query.value(2).toInt();
    if (purchase != 0) {
        payment.purchaseId = purchase;
    }
    payment.amountCents = query.value(3).toLongLong();
    payment.paidAt = query.value(4).toString();
    payment.note = query.value(5).toString();
    payment.createdAt = query.value(6).toString();
    return payment;
}

const char* kPaymentColumns = "id, supplier_id, purchase_id, amount_cents, paid_at, note, created_at";

} // namespace

std::optional<core::SupplierPayment> SupplierPaymentRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM supplier_payments WHERE id = ?").arg(QLatin1StringView(kPaymentColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return paymentFromQuery(query);
}

std::vector<core::SupplierPayment> SupplierPaymentRepository::findBySupplierId(int supplierId) const
{
    std::vector<core::SupplierPayment> payments;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM supplier_payments WHERE supplier_id = ? ORDER BY paid_at DESC")
            .arg(QLatin1StringView(kPaymentColumns)));
    query.addBindValue(supplierId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierPaymentRepository::findBySupplierId"));
        return payments;
    }
    while (query.next()) {
        payments.push_back(paymentFromQuery(query));
    }
    return payments;
}

std::vector<core::SupplierPayment> SupplierPaymentRepository::findByPurchaseId(int purchaseId) const
{
    std::vector<core::SupplierPayment> payments;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM supplier_payments WHERE purchase_id = ? ORDER BY paid_at DESC")
            .arg(QLatin1StringView(kPaymentColumns)));
    query.addBindValue(purchaseId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierPaymentRepository::findByPurchaseId"));
        return payments;
    }
    while (query.next()) {
        payments.push_back(paymentFromQuery(query));
    }
    return payments;
}

std::vector<core::SupplierPayment> SupplierPaymentRepository::findBetween(const QDateTime& from,
                                                                         const QDateTime& to) const
{
    std::vector<core::SupplierPayment> payments;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM supplier_payments WHERE paid_at >= ? AND paid_at <= ? ORDER BY paid_at")
            .arg(QLatin1StringView(kPaymentColumns)));
    query.addBindValue(toIso(from));
    query.addBindValue(toIso(to));
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierPaymentRepository::findBetween"));
        return payments;
    }
    while (query.next()) {
        payments.push_back(paymentFromQuery(query));
    }
    return payments;
}

int SupplierPaymentRepository::insert(const core::SupplierPayment& payment)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO supplier_payments (supplier_id, purchase_id, amount_cents, paid_at, note, "
                       "created_at) VALUES (?, ?, ?, ?, ?, ?)"));
    query.addBindValue(payment.supplierId);
    if (payment.purchaseId.has_value()) {
        query.addBindValue(*payment.purchaseId);
    } else {
        query.addBindValue(QVariant());
    }
    query.addBindValue(payment.amountCents);
    query.addBindValue(payment.paidAt);
    query.addBindValue(payment.note.isNull() ? QStringLiteral("") : payment.note);
    query.addBindValue(payment.createdAt);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierPaymentRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

} // namespace app::data
