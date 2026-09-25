#pragma once

#include <QWidget>

#include <QVector>

namespace app::ui {

// Lightweight QPainter bar chart for daily revenue summaries. Paints flat
// rounded bars with a faint grid — cheap enough for old hardware.
class MiniBarChart : public QWidget {
public:
    using Point = QPair<QString, long long>; // (day label, total cents)

    explicit MiniBarChart(QWidget* parent = nullptr);

    void setData(const QVector<Point>& points);
    void setEmptyMessage(const QString& message);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<Point> m_points;
    QString m_emptyMessage;
};

} // namespace app::ui