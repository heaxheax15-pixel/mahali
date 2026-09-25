#include "app_icon.h"

#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QPixmap>

#include <cmath>

namespace app::ui {

namespace {

// Paints inside a 24x24 box; caller must scale the painter first.
void paintInBox(Icon kind, QPainter& painter, const QColor& color)
{
    QPen pen(color, 1.8f, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QBrush brush(color);
    QPen thin(color, 1.5f, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(Qt::NoPen);

    auto strokeCircle = [&](float cx, float cy, float r) {
        QPen p = thin;
        p.setColor(color);
        painter.setPen(p);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(cx, cy), r, r);
    };
    auto fillCircle = [&](float cx, float cy, float r) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(brush);
        painter.drawEllipse(QPointF(cx, cy), r, r);
    };

    switch (kind) {
    case Icon::Cart: {
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::NoBrush);
        QPainterPath body;
        body.addRoundedRect(QRectF(2.5f, 5.f, 11.f, 8.f), 1.5f, 1.5f);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(body);
        painter.drawLine(QPointF(13.5f, 6.f), QPointF(21.f, 6.f));
        painter.drawLine(QPointF(21.f, 6.f), QPointF(19.5f, 12.f));
        painter.drawLine(QPointF(19.5f, 12.f), QPointF(12.f, 12.f));
        strokeCircle(7.5f, 16.5f, 1.8f);
        strokeCircle(16.f, 16.5f, 1.8f);
        break;
    }
    case Icon::Box: {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(3.f, 10.f, 18.f, 10.f));
        painter.drawLine(QPointF(2.f, 5.f), QPointF(22.f, 5.f));
        painter.drawLine(QPointF(6.f, 5.f), QPointF(6.f, 20.f));
        break;
    }
    case Icon::People: {
        strokeCircle(9.f, 6.5f, 3.f);
        QPainterPath shoulders;
        shoulders.moveTo(3.5f, 20.f);
        shoulders.quadTo(6.f, 13.5f, 9.f, 13.5f);
        shoulders.quadTo(12.f, 13.5f, 14.5f, 20.f);
        shoulders.closeSubpath();
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(shoulders);
        strokeCircle(17.f, 7.f, 2.6f);
        QPainterPath body2;
        body2.moveTo(13.f, 20.f);
        body2.quadTo(14.5f, 14.5f, 17.f, 14.5f);
        body2.quadTo(19.5f, 14.5f, 21.f, 20.f);
        painter.drawPath(body2);
        break;
    }
    case Icon::Truck: {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(2.5f, 8.f, 2.5f + 8.f - 2.5f, 11.f));
        painter.drawRect(QRectF(13.f, 5.f, 7.f, 7.f));
        painter.drawLine(QPointF(13.f, 9.f), QPointF(11.f, 9.f));
        strokeCircle(7.f, 18.f, 1.8f);
        strokeCircle(17.5f, 18.f, 1.8f);
        break;
    }
    case Icon::Wallet: {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(2.5f, 6.5f, 19.f, 12.f), 2.5f, 2.5f);
        painter.drawLine(QPointF(2.5f, 11.f), QPointF(21.5f, 11.f));
        fillCircle(16.f, 14.5f, 1.6f);
        break;
    }
    case Icon::Receipt: {
        QPainterPath path;
        path.moveTo(7.f, 2.5f);
        path.lineTo(17.f, 2.5f);
        path.lineTo(17.f, 19.f);
        path.lineTo(15.f, 18.f);
        path.lineTo(13.f, 19.f);
        path.lineTo(11.f, 18.f);
        path.lineTo(9.f, 19.f);
        path.lineTo(7.f, 18.f);
        path.closeSubpath();
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        painter.drawLine(QPointF(10.f, 7.f), QPointF(14.f, 7.f));
        painter.drawLine(QPointF(10.f, 10.f), QPointF(14.f, 10.f));
        break;
    }
    case Icon::BarChart: {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(3.f, 20.f), QPointF(21.f, 20.f));
        painter.drawRect(QRectF(4.5f, 12.5f, 3.2f, 7.5f));
        painter.drawRect(QRectF(10.5f, 9.f, 3.2f, 11.f));
        painter.drawRect(QRectF(16.5f, 5.5f, 3.2f, 14.5f));
        break;
    }
    case Icon::Tag: {
        QPainterPath ribbon;
        ribbon.addRoundedRect(QRectF(2.f, 8.f, 14.f, 9.f), 2.f, 2.f);
        QPainterPath tail;
        tail.moveTo(9.f, 17.f);
        tail.lineTo(9.f, 21.f);
        tail.lineTo(6.f, 18.5f);
        tail.closeSubpath();
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(ribbon);
        painter.drawPath(tail);
        fillCircle(6.5f, 12.5f, 1.3f);
        break;
    }
    case Icon::Return: {
        QPainterPath curve;
        curve.moveTo(7.5f, 8.f);
        curve.cubicTo(9.5f, 5.5f, 14.f, 5.f, 16.5f, 7.5f);
        curve.cubicTo(18.5f, 9.5f, 18.f, 12.5f, 16.f, 14.5f);
        curve.lineTo(12.f, 16.5f);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(curve);
        painter.drawLine(QPointF(12.f, 16.5f), QPointF(15.f, 15.f));
        painter.drawLine(QPointF(12.f, 16.5f), QPointF(15.5f, 18.f));
        break;
    }
    case Icon::History: {
        strokeCircle(12.f, 12.f, 8.5f);
        painter.setPen(pen);
        painter.drawLine(QPointF(12.f, 12.f), QPointF(12.f, 7.5f));
        painter.drawLine(QPointF(12.f, 12.f), QPointF(15.5f, 14.f));
        painter.drawPoint(QPointF(12.f, 12.f));
        break;
    }
    case Icon::Gear: {
        strokeCircle(12.f, 12.f, 5.5f);
        painter.setPen(thin);
        for (int i = 0; i < 8; ++i) {
            const float angle = i * 45.f * 3.14159f / 180.f;
            const float cx = 12.f + 8.5f * std::cos(angle);
            const float cy = 12.f + 8.5f * std::sin(angle);
            painter.drawLine(QPointF(cx, cy), QPointF(12.f + 10.f * std::cos(angle),
                                                      12.f + 10.f * std::sin(angle)));
        }
        fillCircle(12.f, 12.f, 2.f);
        break;
    }
    case Icon::Search: {
        strokeCircle(9.5f, 10.f, 4.8f);
        painter.setPen(pen);
        painter.drawLine(QPointF(13.5f, 14.f), QPointF(20.f, 19.5f));
        break;
    }
    case Icon::Plus: {
        painter.setPen(pen);
        painter.drawLine(QPointF(12.f, 4.f), QPointF(12.f, 20.f));
        painter.drawLine(QPointF(4.f, 12.f), QPointF(20.f, 12.f));
        break;
    }
    case Icon::Check: {
        painter.setPen(pen);
        painter.drawPolyline(QPolygonF() << QPointF(5.f, 12.5f) << QPointF(10.f, 17.f)
                                         << QPointF(19.5f, 6.f));
        break;
    }
    case Icon::Trash: {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(6.f, 8.f, 12.f, 11.5f));
        painter.drawLine(QPointF(5.f, 8.f), QPointF(19.f, 8.f));
        painter.drawLine(QPointF(9.f, 4.f), QPointF(15.f, 4.f));
        painter.drawLine(QPointF(9.5f, 11.5f), QPointF(9.5f, 16.5f));
        painter.drawLine(QPointF(14.5f, 11.5f), QPointF(14.5f, 16.5f));
        break;
    }
    case Icon::X: {
        painter.setPen(pen);
        painter.drawLine(QPointF(6.f, 6.f), QPointF(18.f, 18.f));
        painter.drawLine(QPointF(18.f, 6.f), QPointF(6.f, 18.f));
        break;
    }
    case Icon::Clock: {
        strokeCircle(12.f, 12.f, 8.5f);
        painter.setPen(pen);
        painter.drawLine(QPointF(12.f, 12.f), QPointF(12.f, 7.5f));
        painter.drawLine(QPointF(12.f, 12.f), QPointF(15.5f, 14.f));
        break;
    }
    case Icon::Info: {
        strokeCircle(12.f, 12.f, 8.5f);
        painter.setPen(pen);
        painter.drawLine(QPointF(12.f, 10.5f), QPointF(12.f, 17.f));
        fillCircle(12.f, 6.8f, 1.3f);
        break;
    }
    case Icon::Sun: {
        strokeCircle(12.f, 12.f, 4.3f);
        painter.setPen(thin);
        for (int i = 0; i < 8; ++i) {
            const float angle = i * 45.f * 3.14159f / 180.f;
            const float r1 = 7.f, r2 = 9.5f;
            painter.drawLine(QPointF(12.f + r1 * std::cos(angle), 12.f + r1 * std::sin(angle)),
                             QPointF(12.f + r2 * std::cos(angle), 12.f + r2 * std::sin(angle)));
        }
        break;
    }
    case Icon::Moon: {
        fillCircle(14.5f, 12.f, 7.f);
        // Punch a hole to shape the crescent.
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        fillCircle(11.f, 9.5f, 6.2f);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        break;
    }
    case Icon::Shop: {
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(3.f, 10.f), QPointF(5.5f, 5.f));
        painter.drawLine(QPointF(5.5f, 5.f), QPointF(12.f, 5.f));
        painter.drawLine(QPointF(12.f, 5.f), QPointF(18.5f, 5.f));
        painter.drawLine(QPointF(18.5f, 5.f), QPointF(21.f, 10.f));
        painter.drawRect(QRectF(4.f, 10.f, 16.f, 9.5f));
        painter.drawRect(QRectF(10.f, 10.f, 4.f, 9.5f));
        break;
    }
    }
}

} // namespace

QIcon appIcon(Icon kind, const QColor& color, int size)
{
    static QHash<QString, QIcon> cache;
    const QString key = QStringLiteral("%1:%2:%3:%4")
                            .arg(static_cast<int>(kind))
                            .arg(color.rgba(), 16, 16)
                            .arg(size);
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return *it;
    }

    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(dpr * size / 24.0, dpr * size / 24.0);
    paintIcon(kind, painter, color, 24.0f);

    const QIcon icon(pm);
    cache.insert(key, icon);
    return icon;
}

// The painter must already be in a 24x24-scaled coordinate space if `s` differs.
void paintIcon(Icon kind, QPainter& painter, const QColor& color, float s)
{
    if (s != 24.0f) {
        painter.save();
        painter.scale(s / 24.0f, s / 24.0f);
        paintInBox(kind, painter, color);
        painter.restore();
    } else {
        paintInBox(kind, painter, color);
    }
}

} // namespace app::ui