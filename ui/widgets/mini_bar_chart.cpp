#include "mini_bar_chart.h"

#include <QPainter>
#include <QPainterPath>

namespace app::ui {

namespace {

QString formatCompact(long long cents)
{
    return QString::number(cents / 100);
}

} // namespace

MiniBarChart::MiniBarChart(QWidget* parent)
    : QWidget(parent)
    , m_emptyMessage(tr("لا بيانات"))
{
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void MiniBarChart::setData(const QVector<Point>& points)
{
    m_points = points;
    update();
}

void MiniBarChart::setEmptyMessage(const QString& message)
{
    m_emptyMessage = message;
    update();
}

QSize MiniBarChart::sizeHint() const
{
    return QSize(420, 170);
}

void MiniBarChart::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();
    const int padL = 26;
    const int padR = 10;
    const int padT = 10;
    const int padB = 26;

    if (m_points.isEmpty()) {
        painter.setPen(QColor(QStringLiteral("#93a3a8")));
        painter.drawText(rect(), Qt::AlignCenter, m_emptyMessage);
        return;
    }

    long long max = 0;
    for (const Point& point : m_points) {
        max = qMax(max, qAbs(point.second));
    }
    if (max == 0) {
        max = 1;
    }

    const int plotLeft = padL;
    const int plotTop = padT;
    const int plotRight = w - padR;
    const int plotBottom = h - padB;
    const int plotW = plotRight - plotLeft;
    const int plotH = plotBottom - plotTop;

    painter.setPen(QPen(QColor(QStringLiteral("#dfe7ee")), 1));
    painter.setBrush(Qt::NoBrush);
    for (int g = 0; g <= 3; ++g) {
        const int y = plotTop + (plotH * g) / 3;
        painter.drawLine(QPoint(plotLeft, y), QPoint(plotRight, y));
    }

    QVector<QPointF> points;
    const int count = m_points.size();
    for (int i = 0; i < count; ++i) {
        const long long value = m_points[i].second;
        const int x = plotLeft + (plotW * i) / qMax(1, count - 1);
        const int y = plotBottom - int((double(qAbs(value)) / double(max)) * (plotH - 8));
        points.append(QPointF(x, y));
    }

    if (points.size() > 1) {
        QPainterPath area;
        QPainterPath line;
        line.moveTo(points.first());
        for (int i = 1; i < points.size(); ++i) {
            line.lineTo(points[i]);
        }

        area.moveTo(points.first().x(), plotBottom);
        area.lineTo(points.first());
        for (int i = 1; i < points.size(); ++i) {
            area.lineTo(points[i]);
        }
        area.lineTo(points.last().x(), plotBottom);
        area.closeSubpath();

        QLinearGradient fillGradient(QPointF(plotLeft, plotTop), QPointF(plotLeft, plotBottom));
        fillGradient.setColorAt(0.0, QColor(QStringLiteral("#8b5cf6")));
        fillGradient.setColorAt(1.0, QColor(QStringLiteral("#dbeafe")));
        painter.setPen(Qt::NoPen);
        painter.setBrush(fillGradient);
        painter.setOpacity(0.22);
        painter.drawPath(area);
        painter.setOpacity(1.0);

        QPen linePen(QColor(QStringLiteral("#4f46e5")), 2.5);
        painter.setPen(linePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(line);
    }

    for (int i = 0; i < count; ++i) {
        const long long value = m_points[i].second;
        const int x = plotLeft + (plotW * i) / qMax(1, count - 1);
        const int y = plotBottom - int((double(qAbs(value)) / double(max)) * (plotH - 8));

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#4f46e5")));
        painter.drawEllipse(QPoint(x, y), 4, 4);

        painter.setPen(QColor(QStringLiteral("#64748b")));
        painter.drawText(QRect(x - 16, plotBottom + 4, 32, 18), Qt::AlignCenter, m_points[i].first);

        if (value != 0) {
            painter.setPen(QColor(QStringLiteral("#334155")));
            painter.drawText(QRect(x - 18, qMax(plotTop, y - 20), 36, 16), Qt::AlignCenter,
                             formatCompact(value));
        }
    }
}

} // namespace app::ui