#pragma once

#include <QFrame>

#include <QLabel>

#include "app_icon.h"
#include "format_utils.h"

class QHBoxLayout;
class QVBoxLayout;

namespace app::ui {

// A compact stat card: a tinted icon chip, a small caption, and a big value.
class StatCard : public QFrame {
public:
    StatCard(const QString& caption, QWidget* parent = nullptr);

    void setIcon(Icon kind, const QString& accentColor);
    void setValue(const QString& text);
    void setCents(long long cents); // formats with the live currency symbol
    void setDelta(long long deltaCents); // +/- suffix, colored by sign

    QLabel* captionLabel() const { return m_caption; }
    QLabel* valueLabel() const { return m_value; }

private:
    QLabel* m_caption = nullptr;
    QLabel* m_value = nullptr;
    QLabel* m_icon = nullptr;
};

} // namespace app::ui