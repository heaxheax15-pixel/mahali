#include "page_header.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace app::ui {

PageHeader::PageHeader(const QString& title, const QString& subtitle, QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("pageHeader"));

    auto* titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("pageHeaderTitle"));

    auto* subtitleLabel = new QLabel(subtitle);
    subtitleLabel->setObjectName(QStringLiteral("pageHeaderSubtitle"));
    subtitleLabel->setWordWrap(true);
    // An empty subtitle is hidden rather than shown blank. A QLabel's height is
    // its font's line height whatever the text is, so an empty label still costs
    // a line and a layout gap: three pages pass QString() because their subtitle
    // repeated something printed a few pixels lower on the same page.
    subtitleLabel->setVisible(!subtitle.isEmpty());
    m_subtitle = subtitleLabel;

    auto* texts = new QVBoxLayout;
    texts->setContentsMargins(0, 0, 0, 0);
    texts->setSpacing(3);
    texts->addWidget(titleLabel);
    texts->addWidget(subtitleLabel);

    m_actionLayout = new QHBoxLayout;
    m_actionLayout->setContentsMargins(0, 0, 0, 0);
    m_actionLayout->setSpacing(10);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(18, 14, 18, 14);
    row->setSpacing(14);
    row->addLayout(texts, 1);
    row->addLayout(m_actionLayout);
}

void PageHeader::setTitle(const QString& title)
{
    for (QObject* child : children()) {
        if (auto* label = qobject_cast<QLabel*>(child);
            label && label->objectName() == QStringLiteral("cardTitle")) {
            label->setText(title);
            return;
        }
    }
}

void PageHeader::setSubtitle(const QString& subtitle)
{
    // Reached through the member rather than by scanning children for an object
    // name: the old lookup searched for "hint", which is not the name this class
    // gives the label ("pageHeaderSubtitle"), so it never matched and the call
    // did nothing. The scan is kept in setTitle()'s shape below only because
    // that one has no caller yet to fix.
    if (!m_subtitle) {
        return;
    }
    m_subtitle->setText(subtitle);
    // Same rule as the constructor: an empty subtitle takes no room.
    m_subtitle->setVisible(!subtitle.isEmpty());
}

void PageHeader::addAction(QWidget* widget)
{
    m_actionLayout->addWidget(widget);
}

QHBoxLayout* PageHeader::actionLayout() const
{
    return m_actionLayout;
}

} // namespace app::ui