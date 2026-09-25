#include "mini_bar_chart.h"

#include <QPainter>

namespace app::ui {

namespace {

QString formatCompact(long long cents)
{
    return QString::number(cents / 100);
}

} // namespace

MiniBarChart::MiniBarChart(QWidget* parent)
    : QWidget(parent)
    , m_emptyMessage(QStringLiteral("لا بيانات"))
{
    setMinimumHeight(130);
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
    return QSize(360, 150);
}

void MiniBarChart::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int w = width();
    const int h = height();
    const int padL = 30; // room for the value labels
    const int padT = 8;

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

    const int bars = m_points.size();
    const int plotH = h - padT - 22;
    const int plotY = padT;
    const int slotW = (w - padL) / bars;

    // Grid: three faint baselines.
    painter.setPen(QPen(QColor(255, 255, 255, 0)));
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(QStringLiteral("#5b9593")), 1));
    for (int g = 0; g <= 3; ++g) {
        const int y = plotY + plotH * g / 3;
        painter.setOpacity(0.12);
        painter.drawLine(QPointF(padL, y), QPointF(w - 4, y));
        painter.setOpacity(1.0);
    }

    painter.setPen(QColor(QStringLiteral("#0e7c75")));
    const QVector<QColor> positives = {QColor(QStringLiteral("#2ba89e")), QColor(QStringLiteral("#0e7c75"))};
    for (int i = 0; i < bars; ++i) {
        const long long value = m_points[i].second;
        const int barH = int(double(qAbs(value)) / double(max) * (plotH - 8));
        const int barW = qMax(6, slotW - 10);
        const int x = padL + i * slotW + (slotW - barW) / 2;
        const int y = plotY + plotH - barH;

        painter.setPen(Qt::NoPen);
        painter.setBrush(positives[i % 2]);
        painter.drawRoundedRect(QRectF(x, y, barW, barH), 4, 4);

        // Value above the bar when the bar is tall enough.
        if (barH > 16) {
            painter.setPen(QColor(QStringLiteral("#6b7a7e")));
            painter.drawText(QRect(x - 4, y - 16, barW + 8, 14), Qt::AlignCenter,
                             formatCompact(value));
        }

        // Day label.
        painter.setPen(QColor(QStringLiteral("#7a8b8f")));
        painter.drawText(QRect(x - 4, plotY + plotH + 4, barW + 8, 16), Qt::AlignCenter,
                         m_points[i].first);
    }
}

} // namespace app::ui