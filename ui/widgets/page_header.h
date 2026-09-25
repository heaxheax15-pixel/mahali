#pragma once

#include <QWidget>

class QHBoxLayout;

namespace app::ui {

// Shared page heading: a big title, a muted subtitle, and an action strip.
class PageHeader : public QWidget {
public:
    PageHeader(const QString& title, const QString& subtitle, QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);
    void addAction(QWidget* widget);
    QHBoxLayout* actionLayout() const;

private:
    QHBoxLayout* m_actionLayout = nullptr;
};

} // namespace app::ui