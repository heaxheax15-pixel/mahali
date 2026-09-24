#include "payment_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

PaymentRepository::PaymentRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::Payment paymentFromQuery(const QSqlQuery& query)
{
    core::Payment payment;
    payment.id = query.value(0).toInt();
    payment.customerId = query.value(1).toInt();
    payment.amountCents = query.value(2).toLongLong();
    payment.createdAt = fromIso(query.value(3).toString()).value_or(QDateTime());
    payment.note = query.value(4).toString();
    payment.reversedId = query.value(5).toInt();
    return payment;
}

const char* kPaymentColumns = "id, customer_id, amount_cents, created_at, note, reversed_id";

} // namespace

std::optional<core::Payment> PaymentRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM payments WHERE id = ?").arg(QLatin1StringView(kPaymentColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return paymentFromQuery(query);
}

std::vector<core::Payment> PaymentRepository::findByCustomerId(int customerId) const
{
    std::vector<core::Payment> payments;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM payments WHERE customer_id = ? ORDER BY created_at")
            .arg(QLatin1StringView(kPaymentColumns)));
    query.addBindValue(customerId);
    if (!query.exec()) {
        return payments;
    }
    while (query.next()) {
        payments.push_back(paymentFromQuery(query));
    }
    return payments;
}

std::vector<core::Payment> PaymentRepository::findBetween(const QDateTime& from, const QDateTime& to) const
{
    std::vector<core::Payment> payments;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM payments WHERE created_at >= ? AND created_at <= ? ORDER BY created_at")
            .arg(QLatin1StringView(kPaymentColumns)));
    query.addBindValue(toIso(from));
    query.addBindValue(toIso(to));
    if (!query.exec()) {
        return payments;
    }
    while (query.next()) {
        payments.push_back(paymentFromQuery(query));
    }
    return payments;
}

int PaymentRepository::insert(const core::Payment& payment)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO payments (customer_id, amount_cents, created_at, note, reversed_id) "
                       "VALUES (?, ?, ?, ?, ?)"));
    query.addBindValue(payment.customerId);
    query.addBindValue(payment.amountCents);
    query.addBindValue(toIso(payment.createdAt.isValid() ? payment.createdAt : QDateTime::currentDateTime()));
    query.addBindValue(payment.note.isNull() ? QStringLiteral("") : payment.note);
    query.addBindValue(payment.reversedId);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

void PaymentRepository::reverse(int originalPaymentId, long long amountCents, const QString& note)
{
    const std::optional<core::Payment> original = findById(originalPaymentId);
    if (!original.has_value()) {
        return;
    }
    core::Payment reversal;
    reversal.customerId = original->customerId;
    reversal.amountCents = -amountCents;
    reversal.createdAt = QDateTime::currentDateTime();
    reversal.note = note;
    reversal.reversedId = originalPaymentId;
    insert(reversal);
}

} // namespace app::data