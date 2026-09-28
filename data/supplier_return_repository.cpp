#include "supplier_return_repository.h"

#include <QSqlQuery>
#include <QVariant>

namespace app::data {

SupplierReturnRepository::SupplierReturnRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::SupplierReturn returnFromQuery(const QSqlQuery& query)
{
    core::SupplierReturn r;
    r.id = query.value(0).toInt();
    r.supplierId = query.value(1).toInt();
    const int purchase = query.value(2).toInt();
    if (purchase != 0) {
        r.purchaseId = purchase;
    }
    r.amountCents = query.value(3).toLongLong();
    r.returnedAt = query.value(4).toString();
    r.removeFromStock = query.value(5).toInt() != 0;
    r.note = query.value(6).toString();
    r.createdAt = query.value(7).toString();
    return r;
}

const char* kReturnColumns =
    "id, supplier_id, purchase_id, amount_cents, returned_at, remove_from_stock, note, created_at";

} // namespace

std::optional<core::SupplierReturn> SupplierReturnRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM supplier_returns WHERE id = ?").arg(QLatin1StringView(kReturnColumns)));
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierReturnRepository::findById"));
        return std::nullopt;
    }
    if (!query.next()) {
        return std::nullopt;
    }
    return returnFromQuery(query);
}

std::vector<core::SupplierReturn> SupplierReturnRepository::findBySupplierId(int supplierId) const
{
    std::vector<core::SupplierReturn> returns;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM supplier_returns WHERE supplier_id = ? ORDER BY returned_at DESC")
            .arg(QLatin1StringView(kReturnColumns)));
    query.addBindValue(supplierId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierReturnRepository::findBySupplierId"));
        return returns;
    }
    while (query.next()) {
        returns.push_back(returnFromQuery(query));
    }
    return returns;
}

std::vector<core::SupplierReturn> SupplierReturnRepository::findByPurchaseId(int purchaseId) const
{
    std::vector<core::SupplierReturn> returns;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM supplier_returns WHERE purchase_id = ? ORDER BY returned_at DESC")
            .arg(QLatin1StringView(kReturnColumns)));
    query.addBindValue(purchaseId);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierReturnRepository::findByPurchaseId"));
        return returns;
    }
    while (query.next()) {
        returns.push_back(returnFromQuery(query));
    }
    return returns;
}

int SupplierReturnRepository::insert(const core::SupplierReturn& r)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO supplier_returns (supplier_id, purchase_id, amount_cents, returned_at, "
                       "remove_from_stock, note, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(r.supplierId);
    if (r.purchaseId.has_value()) {
        query.addBindValue(*r.purchaseId);
    } else {
        query.addBindValue(QVariant());
    }
    query.addBindValue(r.amountCents);
    // returned_at, note and created_at are all NOT NULL, and a null QString is
    // bound as SQL NULL, which the driver refuses. Callers that never set them
    // hold exactly such a null, so the blank the column defaults to is filled in
    // here.
    query.addBindValue(r.returnedAt.isNull() ? QStringLiteral("") : r.returnedAt);
    query.addBindValue(r.removeFromStock ? 1 : 0);
    query.addBindValue(r.note.isNull() ? QStringLiteral("") : r.note);
    query.addBindValue(r.createdAt.isNull() ? QStringLiteral("") : r.createdAt);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierReturnRepository::insert"));
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool SupplierReturnRepository::remove(int id)
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("DELETE FROM supplier_returns WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        m_db.recordError(query.lastError(), QStringLiteral("SupplierReturnRepository::remove"));
        return false;
    }
    return query.numRowsAffected() > 0;
}

} // namespace app::data
