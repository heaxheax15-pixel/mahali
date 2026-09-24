#include "owner_drawing_repository.h"

#include <QSqlQuery>
#include <QVariant>

#include "date_utils.h"

namespace app::data {

OwnerDrawingRepository::OwnerDrawingRepository(Database& db)
    : m_db(db)
{
}

namespace {

core::OwnerDrawing drawingFromQuery(const QSqlQuery& query)
{
    core::OwnerDrawing drawing;
    drawing.id = query.value(0).toInt();
    drawing.createdAt = fromIso(query.value(1).toString()).value_or(QDateTime());
    drawing.amountCents = query.value(2).toLongLong();
    drawing.note = query.value(3).toString();
    drawing.reversedId = query.value(4).toInt();
    return drawing;
}

const char* kDrawingColumns = "id, created_at, amount_cents, note, reversed_id";

} // namespace

std::optional<core::OwnerDrawing> OwnerDrawingRepository::findById(int id) const
{
    QSqlQuery query(m_db.handle());
    query.prepare(QStringLiteral("SELECT %1 FROM owner_drawings WHERE id = ?").arg(QLatin1StringView(kDrawingColumns)));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return drawingFromQuery(query);
}

std::vector<core::OwnerDrawing> OwnerDrawingRepository::findBetween(const QDateTime& from,
                                                                    const QDateTime& to) const
{
    std::vector<core::OwnerDrawing> drawings;
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("SELECT %1 FROM owner_drawings WHERE created_at >= ? AND created_at <= ? ORDER BY created_at")
            .arg(QLatin1StringView(kDrawingColumns)));
    query.addBindValue(toIso(from));
    query.addBindValue(toIso(to));
    if (!query.exec()) {
        return drawings;
    }
    while (query.next()) {
        drawings.push_back(drawingFromQuery(query));
    }
    return drawings;
}

int OwnerDrawingRepository::insert(const core::OwnerDrawing& drawing)
{
    QSqlQuery query(m_db.handle());
    query.prepare(
        QStringLiteral("INSERT INTO owner_drawings (created_at, amount_cents, note, reversed_id) VALUES (?, ?, ?, ?)"));
    query.addBindValue(toIso(drawing.createdAt.isValid() ? drawing.createdAt : QDateTime::currentDateTime()));
    query.addBindValue(drawing.amountCents);
    query.addBindValue(drawing.note);
    query.addBindValue(drawing.reversedId);
    if (!query.exec()) {
        return 0;
    }
    return query.lastInsertId().toInt();
}

void OwnerDrawingRepository::reverse(int originalDrawingId)
{
    const std::optional<core::OwnerDrawing> original = findById(originalDrawingId);
    if (!original.has_value()) {
        return;
    }
    core::OwnerDrawing reversal;
    reversal.createdAt = QDateTime::currentDateTime();
    reversal.amountCents = -original->amountCents;
    reversal.note = original->note;
    reversal.reversedId = originalDrawingId;
    insert(reversal);
}

} // namespace app::data